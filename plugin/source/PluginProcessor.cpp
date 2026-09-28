#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/patch/PatchSerializer.h"
#include "bazalt/engine/nodes/InstanceMixNode.h"
#include "bazalt/engine/nodes/IoNoteInNode.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace bazalt
{
    namespace
    {
        // -30 dB: numerically measurable at the output (so a test can
        // inject a signal into an aux bus and confirm it arrived) without
        // being a meaningful part of the mix. Real sidechain-driven DSP
        // (ducking, modulation) is a future feature once the graph model
        // has aux-typed ports — this is the interim "proof" the milestone
        // asks for, not the final behaviour.
        constexpr float sidechainPassthroughGain = 0.0316f;
    }

    BazaltAudioProcessor::BusesProperties BazaltAudioProcessor::makeBusLayout()
    {
        auto layout = BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true);

        for (int i = 0; i < numAuxBuses; ++i)
            layout = layout.withInput ("Sidechain " + juce::String (i + 1), juce::AudioChannelSet::stereo(), false);

        return layout;
    }

    BazaltAudioProcessor::BazaltAudioProcessor()
        : juce::AudioProcessor (makeBusLayout())
    {
        nodeFactory = bazalt::engine::buildDefaultNodeFactory();
        macroParameters.addParametersTo (*this);
        setDefaultMacroMappings();

        // ~50ms period per ARCHITECTURE.md §3.2 / PlanSwapper.h — engine/
        // has no Timer (headless by design), so the plugin layer is
        // responsible for scheduling every PlanSwapper's reclaim().
        startTimer (50);
    }

    // Hosts are expected to call releaseResources() before destroying a
    // processor, which already stops analysisThread with a bounded timeout.
    // But that contract isn't guaranteed (crashed hosts, test harnesses that
    // skip it), and juce::Thread::~Thread() falls back to an indefinite
    // stopThread(-1) if the thread is still running at that point. Stopping
    // it explicitly here — before telemetryHub starts tearing down — makes
    // shutdown bounded and correct regardless of what the caller did.
    BazaltAudioProcessor::~BazaltAudioProcessor()
    {
        stopTimer();
        analysisThread.stopThread (2000);
    }

    void BazaltAudioProcessor::timerCallback()
    {
        for (auto& swapper : voicePlanSwappers)
            swapper.reclaim();

        globalPlanSwapper.reclaim();
    }

    void BazaltAudioProcessor::setDefaultMacroMappings()
    {
        // Macro 1 -> oscillator shape, 2 -> filter cutoff, 3 -> filter
        // resonance, 4 -> envelope release (ARCHITECTURE.md §4.3's
        // "oscillator shape, filter cutoff/resonance, and envelope").
        // Macros 5-32 are left unmapped — inert automatable floats until
        // something needs them, per "arbitrary, cheap to change" sizing.
        macroMappings = { { 0, "osc", "osc.analog.shape", 0.0f, 3.0f },
                          { 1, "svf", "filter.svf.cutoff", 200.0f, 12000.0f },
                          { 2, "svf", "filter.svf.resonance", 0.3f, 4.0f },
                          { 3, "env", "env.adsr.release", 0.02f, 3.0f } };

        macroParameters.setMappings (macroMappings);
    }

    void BazaltAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        currentSampleRate = sampleRate;
        currentBlockSize = samplesPerBlock;

        instanceMixScratchBuffer.setSize (1, samplesPerBlock);

        // M21: room to snapshot every host input channel (main + 4 aux, stereo)
        // once per block, allocated here so processBlock never allocates.
        hostInputScratch.setSize (bazalt::engine::HostInputs::numAudioBuses * bazalt::engine::HostInputs::channelsPerBus, samplesPerBlock);
        hostInputs = {};
        internalTransportSamples = 0;

        // M17: 200ms hold time, converted from samples-worth-of-silence at
        // this sample rate — matches InstanceMixNode's own parameter
        // default (see PluginProcessor.h's comment on why this is a fixed
        // default rather than read from a real node for now).
        silenceHoldTimeSamples = (int) (0.2 * sampleRate);

        // Compiles and publishes the current graph (the default proof
        // graph, or whatever a patch load already installed) — M7
        // replaces the old one-shot "compile once here" with a real
        // recompile path GraphEditController can call again any time a
        // command edits the graph (NODE_EDITOR.md §6/§7).
        graphEditController.prepare (sampleRate, samplesPerBlock);

        voiceManager.prepare (numVoices);
        macroParameters.prepare (sampleRate);

        // Always stop before re-preparing: prepareToPlay can be called
        // again (e.g. sample rate change) while the thread is running, and
        // touching its scratch buffers/sampleRate from the message thread
        // while it's mid-run() would be a data race.
        analysisThread.stopThread (2000);

        telemetryHub.prepare (8192, bazalt::engine::maxTelemetryFrameBytes); // ADR-0029: room for the largest spectrum (8192-point FFT)

        // The 5 baseline taps (main output + 4 sidechains) are permanently
        // subscribed for the plugin's lifetime — never unsubscribed, so
        // they can never lose their slot to LRU eviction as long as
        // nothing else subscribes more than (maxTaps - 5) additional taps
        // at once. M8's dynamic pool (TelemetryHub.h) is additive to this,
        // not a replacement for it.
        tapPointers[0] = telemetryHub.subscribeTap ("main");
        for (int i = 0; i < numAuxBuses; ++i)
            tapPointers[(size_t) (i + 1)] = telemetryHub.subscribeTap ("aux" + juce::String (i + 1));

        analysisThread.prepare (sampleRate);
        analysisThread.startThread();
    }

    void BazaltAudioProcessor::releaseResources()
    {
        analysisThread.stopThread (2000);
    }

    bool BazaltAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
            return false;

        if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo()
            && ! layouts.getMainInputChannelSet().isDisabled())
            return false;

        for (int i = 0; i < numAuxBuses; ++i)
        {
            const auto auxSet = layouts.getChannelSet (true, i + 1);
            if (! auxSet.isDisabled() && auxSet != juce::AudioChannelSet::stereo())
                return false;
        }

        return true;
    }

    void BazaltAudioProcessor::triggerVoiceNote (bazalt::engine::ExecutionPlan* plan, float pitch, float velocity) noexcept
    {
        if (plan == nullptr)
            return;

        plan->reset(); // fresh phase/envelope/filter state for the (possibly stolen) voice

        // M18 (ADR-0024): the one remaining direct C++ poke — everything
        // downstream (instance.allocator's outputs into osc's "pitch" and
        // env's "gate") is now real graph wiring, not further pokes.
        if (auto* noteIn = findNoteIn (plan))
            noteIn->injectNoteOn (pitch, velocity);
    }

    bazalt::engine::nodes::IoNoteInNode* BazaltAudioProcessor::findNoteIn (bazalt::engine::ExecutionPlan* plan) const noexcept
    {
        return plan == nullptr ? nullptr : dynamic_cast<bazalt::engine::nodes::IoNoteInNode*> (plan->getNodeById (noteInNodeId));
    }

    void BazaltAudioProcessor::pointVoiceTapsAtCurrentVoice (const VoicePlanPtrs& voicePlans) noexcept
    {
        const auto mostRecent = voiceManager.getMostRecentlyTriggeredVoice();
        const auto target = mostRecent >= 0 ? mostRecent : 0;

        for (int i = 0; i < numVoices; ++i)
        {
            auto* plan = voicePlans[(size_t) i];
            const auto shouldBeOn = i == target;

            if (plan != nullptr && plan->arePreviewTapsEnabled() != shouldBeOn)
                plan->setPreviewTapsEnabled (shouldBeOn);
        }
    }

    void BazaltAudioProcessor::handleMidiEvent (const juce::MidiMessage& message, const VoicePlanPtrs& voicePlans)
    {
        // M21: MIDI controller state for io.control - tracked in every mode,
        // omni (the latest value on any channel), so a node placed later still
        // reads the current position of a wheel or pedal the moment it appears.
        if (message.isController())
        {
            const auto controller = message.getControllerNumber();
            if (controller >= 0 && controller < bazalt::engine::HostInputs::numControllers)
                hostInputs.controllers[(size_t) controller] = (float) message.getControllerValue() / 127.0f;
        }
        else if (message.isChannelPressure())
        {
            hostInputs.channelPressure = (float) message.getChannelPressureValue() / 127.0f;
        }
        else if (message.isPitchWheel())
        {
            hostInputs.pitchBend = juce::jlimit (-1.0f, 1.0f, ((float) message.getPitchWheelValue() - 8192.0f) / 8192.0f);
        }

        // A mono graph (no instance.allocator) has no voices: nothing below -
        // allocating one, poking its note-in - applies.
        if (monoOnlyGraph.load (std::memory_order_acquire))
            return;

        if (message.isNoteOn())
        {
            const auto noteId = (bazalt::engine::VoiceManager::NoteId) ((message.getChannel() << 8) | message.getNoteNumber());
            const auto voiceIndex = voiceManager.noteOn (noteId);
            const auto pitch = (float) message.getNoteNumber();
            const auto velocity = message.getFloatVelocity();

            // M20: the preview tap follows whichever voice was just played,
            // whether it went straight to Active or is still fading out its
            // stolen predecessor — same "most recently triggered" value
            // VoiceManager itself now tracks.
            pointVoiceTapsAtCurrentVoice (voicePlans);

            // M17: a stolen voice defers its actual retrigger until its
            // fade-out completes (renderVoiceRange) — DOMAINS.md §5's
            // "faded out over a ramp rather than cut". An idle-voice
            // allocation retriggers immediately, exactly as before.
            if (voiceManager.getStage (voiceIndex) == bazalt::engine::VoiceStage::Stealing)
            {
                voiceManager.setPendingNoteOn (voiceIndex, { noteId, pitch, velocity });
                return;
            }

            triggerVoiceNote (voicePlans[(size_t) voiceIndex], pitch, velocity);
        }
        else if (message.isNoteOff())
        {
            const auto noteId = (bazalt::engine::VoiceManager::NoteId) ((message.getChannel() << 8) | message.getNoteNumber());
            const auto voiceIndex = voiceManager.noteOff (noteId);

            if (voiceIndex >= 0)
                if (auto* noteIn = findNoteIn (voicePlans[(size_t) voiceIndex]))
                    noteIn->injectNoteOff();
        }
        else if (message.isPitchWheel())
        {
            // M18 exit criterion: a pitch-bend render confirms continuous
            // Pitch needs no special-cased path — folded straight into
            // io.noteIn's continuous "pitch" output (ADR-0024), applied to
            // every voice (pitch bend is channel-wide, not per-note, so it
            // isn't routed through VoiceManager's single-target allocation
            // the way note-on/off are).
            constexpr float pitchBendRangeSemitones = 2.0f; // standard default MIDI pitch bend range
            const auto normalized = ((float) message.getPitchWheelValue() - 8192.0f) / 8192.0f; // -1..~1
            const auto bendSemitones = normalized * pitchBendRangeSemitones;

            for (auto* plan : voicePlans)
                if (auto* noteIn = findNoteIn (plan))
                    noteIn->injectPitchBend (bendSemitones);
        }
    }

    namespace
    {
        // M20: which TelemetryFrameType(s) each preview kind actually reads
        // — AnalysisThread skips computing/publishing anything not needed
        // here (TelemetryHub::isFrameTypeNeeded). Kinds with no real
        // producer yet (PreviewDescriptor.h's own "documented for later"
        // list) fall through to the all-true default — harmless (nothing
        // subscribes with one of those kinds today) and forward-compatible
        // once a real one does.
        bazalt::engine::TelemetryFrameTypesNeeded frameTypesNeededFor (bazalt::engine::PreviewKind kind)
        {
            using bazalt::engine::PreviewKind;
            switch (kind)
            {
                case PreviewKind::Waveform: return { true, false, false };
                case PreviewKind::Spectrum: return { false, true, false };
                case PreviewKind::Meter:    return { false, false, true };
                default:                    return {};
            }
        }
    }

    bool BazaltAudioProcessor::attachPreviewSubscription (PreviewSubscription& subscription,
                                                         bazalt::engine::ExecutionPlan* globalPlan,
                                                         const std::vector<bazalt::engine::ExecutionPlan*>& voicePlans)
    {
        // Claim (or re-claim) the hub slot first, whether or not the port
        // resolves yet: subscribeTap is idempotent for a name already held and,
        // if the slot was LRU-evicted in the meantime, claims a fresh one - so
        // asking again on every attach always yields the live Tap*. Holding it
        // while the port is unresolved is what lets the UI's polling find the
        // tap the moment a cable is wired.
        const auto tapName = "node:" + subscription.nodeId + ":" + subscription.portId;
        subscription.tap = telemetryHub.subscribeTap (tapName, frameTypesNeededFor (subscription.kind));

        // ADR-0029: how the tap is analysed - window, FFT size, meter mode - is
        // read from the LIVE node's own preview declaration, so a viewer's
        // settings (its parameters) are what the analysis thread applies. This
        // runs on every attach, and every edit re-attaches, which is exactly
        // what makes a parameter edit take effect. Every voice plan holds an
        // identical node, so the first one found will do. No matching
        // declaration (or no node yet) means the defaults.
        bazalt::engine::Node* node = globalPlan != nullptr ? globalPlan->getNodeById (subscription.nodeId) : nullptr;
        for (auto* plan : voicePlans)
            if (node == nullptr && plan != nullptr)
                node = plan->getNodeById (subscription.nodeId);

        auto settings = bazalt::engine::TapSettings {};
        if (node != nullptr)
        {
            for (const auto& preview : node->getPreviews())
            {
                if (preview.portId == subscription.portId)
                {
                    settings = bazalt::engine::TapSettings::fromPreview (preview);
                    break;
                }
            }
        }

        telemetryHub.setTapSettings (tapName, settings);

        // A node lives in exactly one domain (DomainSplitter's own
        // invariant), so the global plan - when there is one - is checked
        // first and is the simpler case: one plan, one tap.
        if (globalPlan != nullptr)
        {
            // findTappableBufferIndex: an output port's own buffer, or - for a
            // node with no outputs, like view.scope - the buffer wired into
            // its input (ADR-0029).
            const auto bufferIndex = globalPlan->findTappableBufferIndex (subscription.nodeId, subscription.portId);
            if (bufferIndex >= 0)
                return globalPlan->addTapForBufferIndex (bufferIndex, subscription.tap);
        }

        // Voice domain: every voice plan shares one topology, and every one
        // carries the tap; pointVoiceTapsAtCurrentVoice() decides which one
        // actually pushes.
        auto attached = false;
        for (auto* plan : voicePlans)
        {
            if (plan == nullptr)
                continue;

            const auto bufferIndex = plan->findTappableBufferIndex (subscription.nodeId, subscription.portId);
            if (bufferIndex >= 0)
                attached = plan->addTapForBufferIndex (bufferIndex, subscription.tap) || attached;
        }

        return attached;
    }

    namespace
    {
        // Whether a plan's node has a port (input or output) with this id.
        // Message thread only: getInputPorts()/getOutputPorts() allocate.
        bool planHasPort (bazalt::engine::ExecutionPlan* plan, const juce::String& nodeId, const juce::String& portId)
        {
            if (plan == nullptr)
                return false;

            auto* node = plan->getNodeById (nodeId);
            if (node == nullptr)
                return false;

            for (const auto& port : node->getInputPorts())
                if (port.id == portId)
                    return true;

            for (const auto& port : node->getOutputPorts())
                if (port.id == portId)
                    return true;

            return false;
        }

        // The plans currently live in a swapper, as raw pointers, in the shape
        // attachPreviewSubscription() wants.
        std::vector<bazalt::engine::ExecutionPlan*> peekVoicePlans (
            std::array<bazalt::engine::PlanSwapper, BazaltAudioProcessor::numVoices>& swappers)
        {
            std::vector<bazalt::engine::ExecutionPlan*> plans;
            plans.reserve (swappers.size());
            for (auto& swapper : swappers)
                plans.push_back (swapper.peekCurrentPlan());
            return plans;
        }
    }

    bool BazaltAudioProcessor::subscribeVisualizationTap (const juce::String& nodeId, const juce::String& portId, bazalt::engine::PreviewKind kind)
    {
        // The global swapper can still hold a plan from an earlier graph
        // shape, so it only counts while the graph actually has one.
        const auto hasGlobal = hasGlobalDomain.load (std::memory_order_acquire) || monoOnlyGraph.load (std::memory_order_acquire);
        auto* globalPlan = hasGlobal ? globalPlanSwapper.peekCurrentPlan() : nullptr;
        const auto voicePlans = peekVoicePlans (voicePlanSwappers);

        // The port has to exist on the node. Whether anything is wired to it
        // yet is a separate question (below): a viewer is placed before it is
        // connected, and must come alive when it is.
        auto portExists = planHasPort (globalPlan, nodeId, portId);
        for (auto* plan : voicePlans)
            portExists = portExists || planHasPort (plan, nodeId, portId);

        if (! portExists)
            return false;

        PreviewSubscription subscription { nodeId, portId, kind };
        attachPreviewSubscription (subscription, globalPlan, voicePlans); // unresolved is fine: pending

        // Remember it (replacing any earlier entry for the same port, which is
        // how a kind change takes effect) so a recompile can re-attach it.
        for (auto& existing : previewSubscriptions)
        {
            if (existing.nodeId == nodeId && existing.portId == portId)
            {
                existing = std::move (subscription);
                return true;
            }
        }

        previewSubscriptions.push_back (std::move (subscription));
        return true;
    }

    void BazaltAudioProcessor::unsubscribeVisualizationTap (const juce::String& nodeId, const juce::String& portId)
    {
        bazalt::engine::Tap* removedTap = nullptr;
        for (const auto& existing : previewSubscriptions)
            if (existing.nodeId == nodeId && existing.portId == portId)
                removedTap = existing.tap;

        previewSubscriptions.erase (std::remove_if (previewSubscriptions.begin(), previewSubscriptions.end(),
                                                    [&] (const PreviewSubscription& s) { return s.nodeId == nodeId && s.portId == portId; }),
                                    previewSubscriptions.end());

        // Detach from every plan a swapper still holds - including a global
        // plan left over from an earlier graph shape, which is harmless to
        // clear and would otherwise keep a pointer to a released tap slot.
        // Removal is by Tap identity, so it doesn't depend on the port still
        // resolving in the current graph (a view node's input may since have
        // been disconnected).
        if (removedTap != nullptr)
        {
            if (auto* plan = globalPlanSwapper.peekCurrentPlan())
                plan->removeTap (removedTap);

            for (auto* plan : peekVoicePlans (voicePlanSwappers))
                if (plan != nullptr)
                    plan->removeTap (removedTap);
        }

        telemetryHub.unsubscribeTap ("node:" + nodeId + ":" + portId);
    }

    void BazaltAudioProcessor::applyPreviewSubscriptions (const std::vector<bazalt::engine::ExecutionPlan*>& voicePlans,
                                                          bazalt::engine::ExecutionPlan* globalPlan)
    {
        for (auto& subscription : previewSubscriptions)
            attachPreviewSubscription (subscription, globalPlan, voicePlans);

        // Point the voice taps at the current voice before the plans go live.
        // A default-on plan would otherwise push from every voice until the
        // audio thread's next block start corrects it.
        VoicePlanPtrs pointers {};
        for (size_t i = 0; i < pointers.size() && i < voicePlans.size(); ++i)
            pointers[i] = voicePlans[i];

        pointVoiceTapsAtCurrentVoice (pointers);
    }

    void BazaltAudioProcessor::renderVoiceRange (int startSample, int numSamples, const VoicePlanPtrs& voicePlans) noexcept
    {
        if (numSamples <= 0)
            return;

        auto* sum = instanceMixScratchBuffer.getWritePointer (0) + startSample;
        int activeCount = 0;

        for (int voiceIndex = 0; voiceIndex < numVoices; ++voiceIndex)
        {
            const auto stage = voiceManager.getStage (voiceIndex);
            if (stage == bazalt::engine::VoiceStage::Idle)
                continue;

            auto* plan = voicePlans[(size_t) voiceIndex];
            if (plan == nullptr)
                continue;

            if (stage == bazalt::engine::VoiceStage::Stealing)
            {
                // Render the OLD (pre-steal) content for the whole range,
                // ramping it linearly to silence over however many of
                // these samples are still within the fade window
                // (DOMAINS.md §5) — any samples past that are already
                // fully faded (gain 0), not rendered content leaking
                // through.
                const auto fadeSamplesRemainingBefore = voiceManager.getStealFadeSamplesRemaining (voiceIndex);
                const auto fadingSamples = juce::jmin (numSamples, fadeSamplesRemainingBefore);

                processPlanRange (plan, startSample, numSamples);
                const auto* voiceOut = plan->blockBuffers[(size_t) plan->finalOutputBufferIndex].getBlock().getChannelPointer (0);

                const auto startGain = voiceManager.getStealFadeGain (voiceIndex);
                const auto completed = voiceManager.advanceStealFade (voiceIndex, numSamples);
                const auto endGain = voiceManager.getStealFadeGain (voiceIndex);

                for (int i = 0; i < fadingSamples; ++i)
                {
                    const auto t = fadingSamples > 1 ? (float) i / (float) (fadingSamples - 1) : 1.0f;
                    const auto gain = startGain + (endGain - startGain) * t;
                    sum[i] += voiceOut[i] * gain;
                }
                // Samples at/after fadingSamples: gain is 0 — nothing to add.

                ++activeCount;

                if (completed)
                {
                    const auto pending = voiceManager.getPendingNoteOn (voiceIndex);
                    triggerVoiceNote (plan, pending.pitch, pending.velocity);
                    voiceManager.completeSteal (voiceIndex);

                    // The new note starts right where the fade left off,
                    // within this SAME render call — sample-accurate to
                    // within one MIDI-event-boundary sub-range, not
                    // delayed to the next processBlock().
                    const auto remainingSamples = numSamples - fadingSamples;
                    if (remainingSamples > 0)
                    {
                        processPlanRange (plan, startSample + fadingSamples, remainingSamples);
                        const auto* newVoiceOut = plan->blockBuffers[(size_t) plan->finalOutputBufferIndex].getBlock().getChannelPointer (0);
                        for (int i = 0; i < remainingSamples; ++i)
                            sum[fadingSamples + i] += newVoiceOut[i];
                    }
                }

                continue;
            }

            // Active or Releasing.
            processPlanRange (plan, startSample, numSamples);
            const auto* voiceOut = plan->blockBuffers[(size_t) plan->finalOutputBufferIndex].getBlock().getChannelPointer (0);

            for (int i = 0; i < numSamples; ++i)
                sum[i] += voiceOut[i];

            ++activeCount;

            if (stage == bazalt::engine::VoiceStage::Releasing)
            {
                // M17: generic, signal-level silence detection
                // (VoiceManager.h's own comment) — replaces a hardcoded
                // dynamic_cast<AdsrNode*>("env")->isActive() check, so a
                // per-voice delay/reverb tail correctly keeps the instance
                // alive past its envelope's own release.
                float peak = 0.0f;
                for (int i = 0; i < numSamples; ++i)
                    peak = juce::jmax (peak, std::abs (voiceOut[i]));

                if (voiceManager.updateSilenceAndCheckFinished (voiceIndex, peak, numSamples,
                                                                 silenceThresholdLinear, silenceHoldTimeSamples))
                    voiceManager.voiceFinished (voiceIndex);
            }
        }

        activeVoiceCountThisBlock = activeCount;
    }

    // ---- M21 host boundary ------------------------------------------------
    // What io.audioIn / io.control / io.transport read (engine/graph/
    // HostInputs.h). Everything below is audio-thread code: no allocation,
    // no locking, no logging.

    // The main input shares the host buffer's first two channels with the
    // main OUTPUT, so it has to be copied out before anything writes output -
    // and every bus is snapshotted here, once, into the preallocated scratch
    // so the ranges a block is split into can each hand nodes a stable,
    // offset pointer.
    void BazaltAudioProcessor::captureHostInput (juce::AudioBuffer<float>& buffer, int numSamples) noexcept
    {
        constexpr auto channelsPerBus = bazalt::engine::HostInputs::channelsPerBus;

        for (auto& bus : hostBusPresent)
            bus.fill (false);

        if (numSamples > hostInputScratch.getNumSamples())
            return; // a host block bigger than it promised in prepareToPlay: no input beats undefined behaviour

        for (int bus = 0; bus < bazalt::engine::HostInputs::numAudioBuses; ++bus)
        {
            const auto busBuffer = getBusBuffer (buffer, true, bus);
            const auto channels = juce::jmin (channelsPerBus, busBuffer.getNumChannels());

            for (int ch = 0; ch < channels; ++ch)
            {
                hostInputScratch.copyFrom (bus * channelsPerBus + ch, 0, busBuffer, ch, 0, numSamples);
                hostBusPresent[(size_t) bus][(size_t) ch] = true;
            }
        }
    }

    // The transport at the start of this block: the host's playhead when it
    // reports a tempo and beat position, otherwise an internal transport
    // (120 BPM, running) so io.transport still moves in the Standalone app,
    // which has no host timeline.
    void BazaltAudioProcessor::beginTransportForBlock (int numSamples) noexcept
    {
        blockTransport = {};
        auto haveHostTransport = false;

        if (auto* hostPlayHead = getPlayHead())
        {
            if (const auto position = hostPlayHead->getPosition())
            {
                const auto bpm = position->getBpm();
                const auto ppq = position->getPpqPosition();

                if (bpm.hasValue() && ppq.hasValue())
                {
                    blockTransport.playing = position->getIsPlaying();
                    blockTransport.bpm = *bpm;
                    blockTransport.ppq = *ppq;
                    blockTransport.seconds = position->getTimeInSeconds().orFallback (*bpm > 0.0 ? *ppq * 60.0 / *bpm : 0.0);
                    haveHostTransport = true;
                }
            }
        }

        if (! haveHostTransport)
        {
            blockTransport.playing = true;
            blockTransport.bpm = 120.0;
            blockTransport.seconds = (double) internalTransportSamples / currentSampleRate;
            blockTransport.ppq = blockTransport.seconds * blockTransport.bpm / 60.0;
        }

        internalTransportSamples += numSamples;
    }

    // Points hostInputs at the range of samples about to be processed: audio
    // pointers offset to its first sample, transport advanced by the same
    // amount (a stopped transport doesn't move).
    void BazaltAudioProcessor::prepareHostInputsForRange (int startSample) noexcept
    {
        constexpr auto channelsPerBus = bazalt::engine::HostInputs::channelsPerBus;

        for (int bus = 0; bus < bazalt::engine::HostInputs::numAudioBuses; ++bus)
            for (int ch = 0; ch < channelsPerBus; ++ch)
                hostInputs.audio[(size_t) bus][(size_t) ch] = hostBusPresent[(size_t) bus][(size_t) ch]
                                                                 ? hostInputScratch.getReadPointer (bus * channelsPerBus + ch) + startSample
                                                                 : nullptr;

        const auto elapsedSeconds = currentSampleRate > 0.0 ? (double) startSample / currentSampleRate : 0.0;

        hostInputs.transportPlaying = blockTransport.playing;
        hostInputs.tempoBpm = blockTransport.bpm;
        hostInputs.sampleRate = currentSampleRate;
        hostInputs.timeSeconds = blockTransport.seconds + (blockTransport.playing ? elapsedSeconds : 0.0);
        hostInputs.ppqPosition = blockTransport.ppq + (blockTransport.playing ? elapsedSeconds * blockTransport.bpm / 60.0 : 0.0);
    }

    void BazaltAudioProcessor::processPlanRange (bazalt::engine::ExecutionPlan* plan, int startSample, int numSamples) noexcept
    {
        prepareHostInputsForRange (startSample);
        plan->applyHostInputs (hostInputs);
        plan->process (numSamples);
    }

    // A graph with no instance.allocator (DomainSplitter's monoOnly) is one
    // plan, run over every range of the block whether or not any note is
    // held. Its output lands in the same scratch buffer the voice sum uses,
    // so finalizeInstanceMixIntoOutput needs no special case.
    void BazaltAudioProcessor::renderMonoRange (bazalt::engine::ExecutionPlan* plan, int startSample, int numSamples) noexcept
    {
        if (plan == nullptr || numSamples <= 0)
            return;

        processPlanRange (plan, startSample, numSamples);

        const auto* out = plan->blockBuffers[(size_t) plan->finalOutputBufferIndex].getBlock().getChannelPointer (0);
        auto* scratch = instanceMixScratchBuffer.getWritePointer (0) + startSample;

        for (int i = 0; i < numSamples; ++i)
            scratch[i] = out[i];

        activeVoiceCountThisBlock = 0;
    }

    // ARCHITECTURE.md/DOMAINS.md §2: with no instance.mix node in the
    // graph (every M2-M6 patch, and the common case even after M7), the
    // voice sum IS the final output — copied straight to both channels,
    // identical to pre-M7 behaviour. When an instance.mix node exists, the
    // sum (or average, per its mode parameter) is instead handed to it
    // (setExternalBlock) and the GLOBAL plan's own output — not the raw
    // voice sum — reaches the speakers.
    void BazaltAudioProcessor::finalizeInstanceMixIntoOutput (juce::AudioBuffer<float>& output, int numSamples) noexcept
    {
        const float* finalMono = instanceMixScratchBuffer.getReadPointer (0);
        // Real stereo cable redesign (wiki/NODES.System.md §9): set below
        // only when the plan that actually reaches the speakers resolved a
        // real second (right) channel for its designated output — a graph
        // whose output stays plain Mono leaves this null, and the mono-
        // duplicate path at the bottom is unchanged, byte for byte.
        const float* finalRight = nullptr;

        if (! monoRenderedThisBlock && hasGlobalDomain.load (std::memory_order_acquire))
        {
            if (auto* globalPlan = globalPlanSwapper.getCurrentPlanForAudioThread())
            {
                if (globalPlan->externalInputNodeId.isNotEmpty())
                {
                    auto* instanceMixNode = dynamic_cast<bazalt::engine::nodes::InstanceMixNode*> (
                        globalPlan->getNodeById (globalPlan->externalInputNodeId));

                    if (instanceMixNode != nullptr)
                    {
                        if (instanceMixNode->getMode() == bazalt::engine::nodes::InstanceMixNode::Mode::Average
                            && activeVoiceCountThisBlock > 1)
                        {
                            auto* writableMono = instanceMixScratchBuffer.getWritePointer (0);
                            const auto scale = 1.0f / (float) activeVoiceCountThisBlock;
                            for (int i = 0; i < numSamples; ++i)
                                writableMono[i] *= scale;
                        }

                        instanceMixNode->setExternalBlock (finalMono, numSamples);
                        processPlanRange (globalPlan, 0, numSamples); // the global plan runs once over the whole block
                        finalMono = globalPlan->blockBuffers[(size_t) globalPlan->finalOutputBufferIndex]
                                        .getBlock()
                                        .getChannelPointer (0);

                        if (globalPlan->finalOutputBufferIndexRight >= 0)
                            finalRight = globalPlan->blockBuffers[(size_t) globalPlan->finalOutputBufferIndexRight]
                                             .getBlock()
                                             .getChannelPointer (0);
                    }
                }
            }
        }

        auto* left = output.getWritePointer (0);
        auto* right = output.getNumChannels() > 1 ? output.getWritePointer (1) : left;

        if (finalRight != nullptr && right != left)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                left[i] += finalMono[i];
                right[i] += finalRight[i];
            }
        }
        else
        {
            for (int i = 0; i < numSamples; ++i)
            {
                left[i] += finalMono[i];
                right[i] += finalMono[i];
            }
        }
    }

    void BazaltAudioProcessor::updateAuxLevelsAndPassthrough (juce::AudioBuffer<float>& buffer, int numSamples)
    {
        auto* mainLeft = buffer.getWritePointer (0);
        auto* mainRight = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : mainLeft;

        for (int auxIndex = 0; auxIndex < numAuxBuses; ++auxIndex)
        {
            const auto busIndex = auxIndex + 1; // bus 0 is the main input
            auto auxBuffer = getBusBuffer (buffer, true, busIndex);

            float peak = 0.0f;

            if (auxBuffer.getNumChannels() > 0)
            {
                peak = auxBuffer.getMagnitude (0, numSamples);

                for (int ch = 0; ch < auxBuffer.getNumChannels() && ch < 2; ++ch)
                {
                    const auto* src = auxBuffer.getReadPointer (ch);
                    auto* dst = ch == 0 ? mainLeft : mainRight;

                    for (int i = 0; i < numSamples; ++i)
                        dst[i] += src[i] * sidechainPassthroughGain;
                }

                if (auto* tap = tapPointers[(size_t) (auxIndex + 1)])
                    tap->push (auxBuffer.getReadPointer (0), numSamples);
            }

            auxPeakLevels[(size_t) auxIndex].store (peak, std::memory_order_relaxed);
        }
    }

    void BazaltAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
    {
        juce::ScopedNoDenormals noDenormals;

        const auto numSamples = buffer.getNumSamples();

        // M21: snapshot the host's input BEFORE clearing anything. This used to
        // clear every channel first, which wiped the main input (shared with the
        // output) and the sidechain aux inputs before anything could read them -
        // the sidechain meters and passthrough never saw a sample of real audio.
        captureHostInput (buffer, numSamples);
        beginTransportForBlock (numSamples);

        // Clear only the OUTPUT channels. The aux inputs live beyond them in the
        // host buffer and updateAuxLevelsAndPassthrough reads them at the end.
        for (int ch = 0; ch < getTotalNumOutputChannels() && ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, numSamples);

        instanceMixScratchBuffer.clear (0, numSamples);

        // Fetched exactly once per process() call, per PlanSwapper's own
        // contract (PlanSwapper.h) — handleMidiEvent/renderVoiceRange below
        // both read from this same cached array rather than re-querying
        // the swappers mid-block.
        // M21: a graph with no instance.allocator is ONE plan in the global
        // swapper, run every block (see renderMonoRange). Fetched once, like the
        // voice plans, per PlanSwapper's contract.
        const auto monoOnly = monoOnlyGraph.load (std::memory_order_acquire);
        monoRenderedThisBlock = monoOnly;

        VoicePlanPtrs voicePlanPtrs {};
        bazalt::engine::ExecutionPlan* monoPlan = nullptr;

        if (monoOnly)
        {
            monoPlan = globalPlanSwapper.getCurrentPlanForAudioThread();
            bazalt::engine::ExecutionPlan* monoPlans[1] = { monoPlan };
            macroParameters.applyToPlans (monoPlans, 1, numSamples);
        }
        else
        {
            for (int i = 0; i < numVoices; ++i)
                voicePlanPtrs[(size_t) i] = voicePlanSwappers[(size_t) i].getCurrentPlanForAudioThread();

            macroParameters.applyToPlans (voicePlanPtrs.data(), numVoices, numSamples);

            // ADR-0029: switch the preview taps on for exactly one voice plan. Cheap
            // (8 relaxed loads), and it also corrects the fresh plans a publish just
            // made live, so it is not enough to rely on the note-on path alone.
            pointVoiceTapsAtCurrentVoice (voicePlanPtrs);
        }

        auto renderRange = [&] (int start, int count)
        {
            if (monoOnly)
                renderMonoRange (monoPlan, start, count);
            else
                renderVoiceRange (start, count, voicePlanPtrs);
        };

        int previousSample = 0;

        for (const auto metadata : midiMessages)
        {
            const auto eventSample = metadata.samplePosition;

            if (eventSample > previousSample)
                renderRange (previousSample, eventSample - previousSample);

            handleMidiEvent (metadata.getMessage(), voicePlanPtrs);
            previousSample = eventSample;
        }

        if (previousSample < numSamples)
            renderRange (previousSample, numSamples - previousSample);

        finalizeInstanceMixIntoOutput (buffer, numSamples);

        updateAuxLevelsAndPassthrough (buffer, numSamples);

        if (auto* mainTap = tapPointers[0])
            mainTap->push (buffer.getReadPointer (0), buffer.getNumSamples());

        outputGuard.process (buffer);
    }

    juce::AudioProcessorEditor* BazaltAudioProcessor::createEditor()
    {
        return new BazaltAudioProcessorEditor (*this);
    }

    const juce::String BazaltAudioProcessor::getName() const
    {
        return JucePlugin_Name;
    }

    bazalt::engine::PatchDocument BazaltAudioProcessor::getCurrentPatchDocument() const
    {
        auto doc = bazalt::engine::PatchDocument::fromNodeGraph (graphEditController.getGraph());

        doc.macroMappings = macroMappings;
        doc.macroValues = macroParameters.getCurrentValues();
        doc.meta.name = "Bazalt Init";
        doc.meta.modifiedAtMs = juce::Time::getCurrentTime().toMilliseconds();

        return doc;
    }

    juce::String BazaltAudioProcessor::getStateAsJson() const
    {
        return bazalt::engine::serializePatchToJson (getCurrentPatchDocument());
    }

    bool BazaltAudioProcessor::loadStateFromJson (const juce::String& json)
    {
        auto result = bazalt::engine::parsePatchFromJson (json);
        if (! result.success)
            return false;

        // Graph first: if the loaded patch's graph doesn't compile, bail
        // out before touching macros too — never leave the graph and
        // macro state representing two different patches (CLAUDE.md rule
        // 5's "never let a bad edit reach the audio thread" applies to a
        // whole-state load exactly as it does to a single command).
        const auto graphResult = graphEditController.setGraph (result.document.toNodeGraph());
        if (! graphResult.success)
            return false;

        macroMappings = result.document.macroMappings;
        macroParameters.setMappings (macroMappings);
        macroParameters.setValuesForLoadedPatch (result.document.macroValues);

        return true;
    }

    float BazaltAudioProcessor::getAuxPeakLevel (int auxIndex) const noexcept
    {
        if (auxIndex < 0 || auxIndex >= numAuxBuses)
            return 0.0f;

        return auxPeakLevels[(size_t) auxIndex].load (std::memory_order_relaxed);
    }

    void BazaltAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        const auto json = getStateAsJson();
        const auto* utf8 = json.toRawUTF8();
        destData.append (utf8, strlen (utf8));
    }

    void BazaltAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        const auto json = juce::String::fromUTF8 (static_cast<const char*> (data), sizeInBytes);
        loadStateFromJson (json);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new bazalt::BazaltAudioProcessor();
}
