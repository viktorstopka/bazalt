#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/patch/PatchSerializer.h"
#include "bazalt/engine/nodes/InstanceMixNode.h"
#include "bazalt/engine/nodes/InstanceOriginNode.h"
#include "bazalt/engine/nodes/IoNoteInNode.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace bazalt
{
    namespace
    {

        // DomainRedesign.md Batch 2's MIDI-independence mechanism: a
        // sentinel NoteId for a voice triggered by an origin's own internal
        // graph wiring (clock -> seq -> note.assemble -> spawn) rather than
        // real host MIDI. Real MIDI NoteIds are (channel << 8) | noteNumber,
        // channel 1-16 and note 0-127, so the largest real value is
        // (16 << 8) | 127 = 4223 — nowhere near this.
        constexpr bazalt::engine::VoiceManager::NoteId internalTriggerNoteId = 0xFFFFFFFFu;
    }

    BazaltAudioProcessor::BusesProperties BazaltAudioProcessor::makeBusLayout()
    {
        auto layout = BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true);

        // Sidechain 1 is on from the start: Ableton Live never activates a VST3
        // aux bus that starts inactive, so a sidechain routed to Bazalt there
        // reached nothing (the diagnostics log showed "Sidechain 1=off" with
        // Live's sidechain set). 2-4 stay off until a host asks for them.
        for (int i = 0; i < numAuxBuses; ++i)
            layout = layout.withInput ("Sidechain " + juce::String (i + 1), juce::AudioChannelSet::stereo(), i == 0);

        return layout;
    }

    BazaltAudioProcessor::BazaltAudioProcessor()
        : juce::AudioProcessor (makeBusLayout())
    {
        nodeFactory = bazalt::engine::buildDefaultNodeFactory();
        macroParameters.addParametersTo (*this);
        // No default mappings seeded here any more (wiki/plans/UtilMacro.md)
        // — the starting graph (ProofGraphs.h::buildMasterOutOnlyGraph()) has
        // no util.macro nodes, so the very first recompileAndPublish() that
        // runs on construction derives an empty mapping list, same effect as
        // the old setDefaultMacroMappings() had once the default graph
        // stopped being buildVoiceProofGraph() (its 4 fixed entries targeted
        // node ids that graph no longer has).

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
        globalPlanSwapper.reclaim();

        // TEMPORARY diagnostic — see the member declarations' own comment.
        const auto current = processBlockCallCount.load (std::memory_order_relaxed);
        const auto nowMs = juce::Time::getMillisecondCounter();
        if (current != lastLoggedProcessBlockCallCount && nowMs - lastProcessBlockLogMs >= 5000)
        {
            lastProcessBlockLogMs = nowMs;
            // Which input buses the host has switched on, and how loud each
            // is — answers "does the sidechain reach Bazalt at all?" without
            // a debugger (Ableton's Sidechain is input bus 1, "Sidechain 1").
            juce::String inputs;
            for (int bus = 0; bus < getBusCount (true); ++bus)
            {
                const auto* b = getBus (true, bus);
                const auto peak = bus == 0 ? mainInputPeakLevel.load (std::memory_order_relaxed)
                                           : auxPeakLevels[(size_t) (bus - 1)].load (std::memory_order_relaxed);
                inputs << " " << (b != nullptr ? b->getName() : juce::String (bus)) << "="
                       << (b != nullptr && b->isEnabled() ? juce::String (b->getNumberOfChannels()) + "ch peak "
                                                                + juce::String (peak, 4)
                                                          : juce::String ("off"));
            }
            logDiagnostic ("processBlock total calls so far: " + juce::String ((juce::int64) current)
                            + " (sampleRate=" + juce::String (currentSampleRate)
                            + ", blockSize=" + juce::String (currentBlockSize) + ") inputs:" + inputs);
            lastLoggedProcessBlockCallCount = current;
        }
    }

    // TEMPORARY diagnostic — see the member declarations' own comment.
    // Message-thread only (called from prepareToPlay()/timerCallback(),
    // never from processBlock() itself). Appends one line, with a
    // millisecond timestamp, to a fixed, easy-to-find log file.
    void BazaltAudioProcessor::logDiagnostic (const juce::String& line) const
    {
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("BazaltDiagnostics.log");
        // Capped: past 1 MB the log moves to BazaltDiagnostics.old.log (replacing
        // the previous one) and starts over, so it never grows without bound.
        if (file.getSize() > 1024 * 1024)
            file.moveFileTo (file.getSiblingFile ("BazaltDiagnostics.old.log"));
        file.appendText ("[" + juce::String (juce::Time::getCurrentTime().toMilliseconds()) + "] [instance "
                          + instanceId.toString().substring (0, 8) + "] " + line + "\n");
    }

    void BazaltAudioProcessor::commitOriginBundleAssignments (const std::array<juce::String, maxOrigins>& originIdBySlot) noexcept
    {
        for (int i = 0; i < maxOrigins; ++i)
        {
            auto& bundle = originBundles[(size_t) i];
            const auto& newId = originIdBySlot[(size_t) i];

            if (newId.isEmpty())
            {
                bundle.active = false;
                bundle.originNodeId = {};
                continue;
            }

            if (bundle.originNodeId != newId)
            {
                // A different origin now occupies this slot — its
                // predecessor's voice state (which lanes are Active/Idle)
                // means nothing here; start fresh, exactly like a brand-new
                // origin would.
                bundle.voiceManager.prepare (numVoices);
                bundle.originNodeId = newId;
            }
            // else: the SAME origin persists across this edit — its
            // VoiceManager is untouched, same principle as GraphCompiler's
            // own per-node DSP-state reuse, just one level up (which voices
            // are sounding, not a single node's own internal state).

            bundle.active = true;
        }
    }

    void BazaltAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        // TEMPORARY diagnostic — see the member declarations' own comment.
        ++prepareToPlayCallCount;

        // A real, confirmed-live gap, found while investigating a real-host-only
        // audio glitch ("retriggered constantly" / heavy delay, reported only
        // under Ableton, never reproduced under the Standalone app): every call
        // here used to unconditionally do a full re-initialization, regardless
        // of whether sampleRate/samplesPerBlock had actually changed since the
        // last call — a full graph recompile (GraphCompiler's own previousPlan
        // reuse keeps each UNCHANGED node's own internal state, e.g. an
        // oscillator's phase, but the recompile itself is still real, non-free
        // work), EVERY origin bundle's own active/voice state reset to
        // inactive, every macro's smoother snapped back to 0.0 (MacroParameters
        // ::prepare() calls reset(0.0f)) even if the host parameter itself is
        // non-zero, AND analysisThread stopped and restarted with up to a
        // 2-SECOND blocking timeout. Many real hosts call prepareToPlay() more
        // than "only once, at startup, and again only on a genuine sample-rate/
        // buffer-size change" — the JUCE/VST3 contract never promised otherwise.
        // Skip all of it when nothing has actually changed: a safe, correct,
        // no-op repeat, not a special case for any one host.
        const auto isRedundantRepeat = hasBeenPrepared
                                        && sampleRate == currentSampleRate
                                        && samplesPerBlock == currentBlockSize;

        logDiagnostic ("prepareToPlay() call #" + juce::String (prepareToPlayCallCount)
                        + " sampleRate=" + juce::String (sampleRate)
                        + " samplesPerBlock=" + juce::String (samplesPerBlock)
                        + (isRedundantRepeat ? " (REDUNDANT - skipped)" : " (real re-init)"));

        if (isRedundantRepeat)
        {
            // Nothing to rebuild, but a releaseResources() in between stopped
            // the analysis thread: Ableton deactivates and reactivates the
            // plugin around a bus-layout change (switching its sidechain on),
            // and scopes and meters stayed dead after that while the audio ran.
            if (! analysisThread.isThreadRunning())
                analysisThread.startThread();
            return;
        }

        hasBeenPrepared = true;
        currentSampleRate = sampleRate;
        currentBlockSize = samplesPerBlock;

        outputLimiter.prepare (sampleRate);

        renderScratchBuffer.setSize (2, samplesPerBlock);

        for (auto& bundle : originBundles)
        {
            bundle.voiceManager.prepare (numVoices);
            bundle.active = false;
            bundle.originNodeId = {};
        }

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

        macroParameters.prepare (sampleRate);
        liveParameterEdits.prepare (sampleRate);

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

        // Inputs may be stereo, mono or off: a host can offer its sidechain
        // in mono, and refusing that left the sidechain silently disabled.
        const auto inputOk = [] (const juce::AudioChannelSet& set)
        {
            return set.isDisabled() || set == juce::AudioChannelSet::stereo() || set == juce::AudioChannelSet::mono();
        };

        if (! inputOk (layouts.getMainInputChannelSet()))
            return false;

        for (int i = 0; i < numAuxBuses; ++i)
            if (! inputOk (layouts.getChannelSet (true, i + 1)))
                return false;

        return true;
    }

    void BazaltAudioProcessor::handleMidiEvent (const juce::MidiMessage& message)
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

        handleNoteEvent (message, planThisBlock);
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
                case PreviewKind::Waveform:       return { true, false, false, false, false, false };
                case PreviewKind::Spectrum:       return { false, true, false, false, false, false };
                case PreviewKind::Meter:          return { false, false, true, false, false, false };
                case PreviewKind::EventImpulse:   return { false, false, false, true, false, false };
                case PreviewKind::RollingHistory: return { false, false, false, false, true, false };
                case PreviewKind::PhaseLocked:    return { false, false, false, false, false, true };
                default:                          return {};
            }
        }
    }

    bool BazaltAudioProcessor::attachPreviewSubscription (PreviewSubscription& subscription, bazalt::engine::ExecutionPlan* plan)
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
        // what makes a parameter edit take effect. An instanced node answers by
        // its own id with instance 0's copy. No matching declaration (or no
        // node yet) means the defaults.
        auto* node = plan != nullptr ? plan->getNodeById (subscription.nodeId) : nullptr;
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

        if (plan == nullptr)
            return false;

        // findTappableBufferIndex: an output port's own buffer, or - for a node
        // with no outputs, like view.scope - the buffer wired into its input
        // (ADR-0029).
        const auto bufferIndex = plan->findTappableBufferIndex (subscription.nodeId, subscription.portId);
        if (bufferIndex >= 0)
            return plan->addTapForBufferIndex (bufferIndex, subscription.tap);

        // A node that runs per instance has one buffer per instance; the tap
        // sits on all of them and the plan pushes only the instance its
        // group's previews show (InstanceGroupRuntime::previewInstance).
        auto attached = false;
        for (const auto index : plan->findTappableBufferIndicesForAllInstances (subscription.nodeId, subscription.portId))
            attached = plan->addTapForBufferIndex (index, subscription.tap) || attached;
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

    }

    bool BazaltAudioProcessor::subscribeVisualizationTap (const juce::String& nodeId, const juce::String& portId, bazalt::engine::PreviewKind kind)
    {
        auto* plan = globalPlanSwapper.peekCurrentPlan();

        // The port has to exist on the node. Whether anything is wired to it
        // yet is a separate question (below): a viewer is placed before it is
        // connected, and must come alive when it is.
        if (! planHasPort (plan, nodeId, portId))
        {
            logDiagnostic ("subscribeVisualizationTap: " + nodeId + ":" + portId + " - PORT DOES NOT EXIST in the plan, rejected");
            return false;
        }

        PreviewSubscription subscription { nodeId, portId, kind };
        const auto attached = attachPreviewSubscription (subscription, plan); // unresolved is fine: pending
        logDiagnostic ("subscribeVisualizationTap: " + nodeId + ":" + portId + " kind=" + juce::String ((int) kind)
                        + " portExists=true attached=" + juce::String (attached ? "true" : "false"));

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
        logDiagnostic ("unsubscribeVisualizationTap: " + nodeId + ":" + portId);
        bazalt::engine::Tap* removedTap = nullptr;
        for (const auto& existing : previewSubscriptions)
            if (existing.nodeId == nodeId && existing.portId == portId)
                removedTap = existing.tap;

        previewSubscriptions.erase (std::remove_if (previewSubscriptions.begin(), previewSubscriptions.end(),
                                                    [&] (const PreviewSubscription& s) { return s.nodeId == nodeId && s.portId == portId; }),
                                    previewSubscriptions.end());

        // Removal is by Tap identity, so it doesn't depend on the port still
        // resolving in the current graph (a view node's input may since have
        // been disconnected).
        if (removedTap != nullptr)
            if (auto* plan = globalPlanSwapper.peekCurrentPlan())
                plan->removeTap (removedTap);

        telemetryHub.unsubscribeTap ("node:" + nodeId + ":" + portId);
    }

    void BazaltAudioProcessor::applyPreviewSubscriptions (bazalt::engine::ExecutionPlan* plan)
    {
        // TEMPORARY diagnostic — visualization investigation: this runs on
        // every recompile (every graph edit) and logs, per live subscription,
        // whether re-attaching it to the freshly compiled plan succeeded.
        if (! previewSubscriptions.empty())
            logDiagnostic ("applyPreviewSubscriptions: re-attaching " + juce::String ((int) previewSubscriptions.size())
                            + " live subscription(s)");

        for (auto& subscription : previewSubscriptions)
        {
            const auto attached = attachPreviewSubscription (subscription, plan);
            logDiagnostic ("  re-attach " + subscription.nodeId + ":" + subscription.portId
                            + " -> " + juce::String (attached ? "OK" : "FAILED (port not found in the plan this compile)"));
        }
    }

    namespace
    {
        // A plan's right output channel when its designated output is stereo
        // (wiki/plans/StereoChannels.md), else its left one — a mono voice
        // contributes equally to both sides of the sum.
        const float* rightOutputOf (const bazalt::engine::ExecutionPlan& plan, const float* left) noexcept
        {
            return plan.finalOutputBufferIndexRight >= 0
                       ? plan.blockBuffers[(size_t) plan.finalOutputBufferIndexRight].getBlock().getChannelPointer (0)
                       : left;
        }
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
            if (bus >= getBusCount (true))
                break;

            // Only the bus's channels the host buffer really holds: a host (or
            // a test) may hand over fewer channels than the layout declares,
            // and getBusBuffer would point past the end.
            const auto firstChannel = getChannelIndexInProcessBlockBuffer (true, bus, 0);
            const auto channels = juce::jlimit (0, channelsPerBus,
                                                juce::jmin (getChannelCountOfBus (true, bus), buffer.getNumChannels() - firstChannel));
            const juce::AudioBuffer<float> busBuffer (buffer.getArrayOfWritePointers() + firstChannel, juce::jmax (0, channels), numSamples);

            for (int ch = 0; ch < channels; ++ch)
            {
                hostInputScratch.copyFrom (bus * channelsPerBus + ch, 0, busBuffer, ch, 0, numSamples);
                hostBusPresent[(size_t) bus][(size_t) ch] = true;
            }

            // A mono bus (a host may hand the sidechain over in mono) feeds
            // both of Audio In's outputs.
            if (channels == 1)
            {
                hostInputScratch.copyFrom (bus * channelsPerBus + 1, 0, busBuffer, 0, 0, numSamples);
                hostBusPresent[(size_t) bus][1] = true;
            }

            if (bus == 0)
                mainInputPeakLevel.store (channels > 0 ? busBuffer.getMagnitude (0, numSamples) : 0.0f, std::memory_order_relaxed);
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

    // The block's rendered output (renderScratchBuffer, range by range) into
    // the host buffer: a mono output feeds both sides.
    void BazaltAudioProcessor::writeOutput (juce::AudioBuffer<float>& output, int numSamples) noexcept
    {
        auto* left = output.getWritePointer (0);
        auto* right = output.getNumChannels() > 1 ? output.getWritePointer (1) : left;
        const auto* renderedLeft = renderScratchBuffer.getReadPointer (0);
        const auto* renderedRight = renderScratchBuffer.getReadPointer (1);

        for (int i = 0; i < numSamples; ++i)
            left[i] += renderedLeft[i];
        if (right != left)
            for (int i = 0; i < numSamples; ++i)
                right[i] += renderedRight[i];
    }

    void BazaltAudioProcessor::updateAuxLevels (int numSamples)
    {
        // Reads the copy captureHostInput made, never the host buffer itself:
        // by now that buffer holds Bazalt's OUTPUT, and an input bus can share
        // its channels (an instrument whose main input the host switched off
        // has Sidechain 1 in channels 0-1) — reading it here measured and
        // re-mixed Bazalt's own output instead of the sidechain.
        constexpr auto channelsPerBus = bazalt::engine::HostInputs::channelsPerBus;
        const auto captured = numSamples <= hostInputScratch.getNumSamples();

        for (int auxIndex = 0; auxIndex < numAuxBuses; ++auxIndex)
        {
            const auto busIndex = auxIndex + 1; // bus 0 is the main input
            float peak = 0.0f;

            if (captured && hostBusPresent[(size_t) busIndex][0])
            {
                const auto first = busIndex * channelsPerBus;
                peak = hostInputScratch.getMagnitude (first, 0, numSamples);

                if (auto* tap = tapPointers[(size_t) (auxIndex + 1)])
                    tap->push (hostInputScratch.getReadPointer (first), numSamples);
            }

            auxPeakLevels[(size_t) auxIndex].store (peak, std::memory_order_relaxed);
        }
    }

    // ---- The instance axis (wiki/plans/InstanceAxis.md) -------------------
    // One plan for the whole patch; each allocator is a group of it whose
    // instances its bundle's VoiceManager allocates. Steal fades, release by
    // silence, internally triggered origins and swarm populations are decided
    // here, per instance, and the plan runs once per range. A patch with no
    // allocator is the same plan with no groups: it simply runs.

    void BazaltAudioProcessor::startInstance (bazalt::engine::ExecutionPlan& plan, int group, int instance, float pitch, float velocity, bool viaAllocator) noexcept
    {
        plan.resetInstance (group, instance); // fresh phase/envelope/filter state for this instance only
        auto& runtime = *plan.instanceGroups[(size_t) group];
        if (viaAllocator)
        {
            if (auto* allocator = dynamic_cast<bazalt::engine::nodes::InstanceOriginNode*> (runtime.originNodes[(size_t) instance]))
                allocator->spawnInstance (pitch, velocity);
        }
        else if (auto* noteIn = dynamic_cast<bazalt::engine::nodes::IoNoteInNode*> (runtime.noteInNodes[(size_t) instance]))
        {
            noteIn->injectNoteOn (pitch, velocity);
        }
    }

    void BazaltAudioProcessor::handleNoteEvent (const juce::MidiMessage& message, bazalt::engine::ExecutionPlan* plan) noexcept
    {
        if (plan == nullptr)
            return;

        for (int g = 0; g < (int) plan->instanceGroups.size(); ++g)
        {
            auto& runtime = *plan->instanceGroups[(size_t) g];
            // Only a group that hears notes takes MIDI: an internally triggered
            // one has its own trigger chain.
            if (runtime.bundleSlot < 0 || runtime.noteInNodes.empty() || runtime.noteInNodes[0] == nullptr)
                continue;
            auto& bundle = originBundles[(size_t) runtime.bundleSlot];
            if (! bundle.active)
                continue;

            if (message.isNoteOn())
            {
                const auto noteId = (bazalt::engine::VoiceManager::NoteId) ((message.getChannel() << 8) | message.getNoteNumber());
                const auto pitch = (float) message.getNoteNumber();
                const auto velocity = message.getFloatVelocity();
                const auto instance = bundle.voiceManager.noteOn (noteId);
                if (instance < 0 || instance >= runtime.count)
                    continue;
                runtime.previewInstance.store (instance, std::memory_order_relaxed);
                if (bundle.voiceManager.getStage (instance) == bazalt::engine::VoiceStage::Stealing)
                    bundle.voiceManager.setPendingNoteOn (instance, { noteId, pitch, velocity });
                else
                    startInstance (*plan, g, instance, pitch, velocity, false);
            }
            else if (message.isNoteOff())
            {
                const auto noteId = (bazalt::engine::VoiceManager::NoteId) ((message.getChannel() << 8) | message.getNoteNumber());
                const auto instance = bundle.voiceManager.noteOff (noteId);
                if (instance >= 0 && instance < runtime.count)
                    if (auto* noteIn = dynamic_cast<bazalt::engine::nodes::IoNoteInNode*> (runtime.noteInNodes[(size_t) instance]))
                        noteIn->injectNoteOff();
            }
            else if (message.isPitchWheel())
            {
                constexpr float pitchBendRangeSemitones = 2.0f;
                const auto bend = ((float) message.getPitchWheelValue() - 8192.0f) / 8192.0f * pitchBendRangeSemitones;
                for (auto* node : runtime.noteInNodes)
                    if (auto* noteIn = dynamic_cast<bazalt::engine::nodes::IoNoteInNode*> (node))
                        noteIn->injectPitchBend (bend);
            }
        }
    }

    void BazaltAudioProcessor::renderRange (bazalt::engine::ExecutionPlan* plan, int startSample, int numSamples) noexcept
    {
        using bazalt::engine::VoiceStage;

        if (numSamples <= 0)
            return;
        if (plan == nullptr)
        {
            renderScratchBuffer.clear (startSample, numSamples);
            return;
        }

        while (numSamples > 0)
        {
            // A steal that finishes inside this range splits it there: the new
            // note starts exactly where the old one faded out.
            auto chunk = numSamples;
            for (auto& group : plan->instanceGroups)
            {
                if (group->bundleSlot < 0)
                    continue;
                auto& bundle = originBundles[(size_t) group->bundleSlot];
                if (! bundle.active || bundle.swarmPopulationSize.load (std::memory_order_relaxed) >= 0)
                    continue;
                for (int i = 0; i < group->count; ++i)
                    if (bundle.voiceManager.getStage (i) == VoiceStage::Stealing)
                        chunk = juce::jmin (chunk, juce::jmax (1, bundle.voiceManager.getStealFadeSamplesRemaining (i)));
            }

            for (int g = 0; g < (int) plan->instanceGroups.size(); ++g)
            {
                auto& group = *plan->instanceGroups[(size_t) g];
                for (auto& state : group.instances)
                    state = {};
                if (group.bundleSlot < 0)
                    continue;
                auto& bundle = originBundles[(size_t) group.bundleSlot];
                if (! bundle.active)
                    continue;

                // A swarm population: its first N instances always live.
                if (const auto population = bundle.swarmPopulationSize.load (std::memory_order_relaxed); population >= 0)
                {
                    for (int i = 0; i < juce::jmin (population, group.count); ++i)
                        group.instances[(size_t) i].run = group.instances[(size_t) i].audible = true;
                    continue;
                }

                // An internally triggered origin (no note-in): its own trigger
                // chain runs in instance 0, which tells us when to spawn or
                // release; the spawn moves to the instance the VoiceManager
                // picks.
                const auto internallyTriggered = group.noteInNodes.empty() || group.noteInNodes[0] == nullptr;
                if (internallyTriggered)
                    if (auto* allocator = dynamic_cast<bazalt::engine::nodes::InstanceOriginNode*> (group.originNodes[0]))
                        if (allocator->consumeSpawnEventsThisBlock() > 0)
                        {
                            if (allocator->getGate())
                            {
                                const auto pitch = allocator->getPitch();
                                const auto velocity = allocator->getVelocity();
                                const auto instance = bundle.voiceManager.noteOn (internalTriggerNoteId);
                                group.previewInstance.store (instance, std::memory_order_relaxed);
                                if (bundle.voiceManager.getStage (instance) == VoiceStage::Stealing)
                                    bundle.voiceManager.setPendingNoteOn (instance, { internalTriggerNoteId, pitch, velocity });
                                else if (instance != 0)
                                    startInstance (*plan, g, instance, pitch, velocity, true);
                            }
                            else
                            {
                                const auto instance = bundle.voiceManager.noteOff (internalTriggerNoteId);
                                if (instance > 0)
                                    if (auto* other = dynamic_cast<bazalt::engine::nodes::InstanceOriginNode*> (group.originNodes[(size_t) instance]))
                                        other->releaseInstance();
                            }
                        }

                for (int i = 0; i < group.count; ++i)
                {
                    auto& state = group.instances[(size_t) i];
                    const auto stage = bundle.voiceManager.getStage (i);
                    if (stage == VoiceStage::Idle)
                    {
                        state.run = i == 0; // instance 0 always runs, so a trigger it hears is noticed
                        continue;
                    }
                    state.run = state.audible = true;
                    if (stage == VoiceStage::Stealing)
                    {
                        state.fading = true;
                        state.rampSamples = juce::jmin (chunk, bundle.voiceManager.getStealFadeSamplesRemaining (i));
                        state.gainStart = bundle.voiceManager.getStealFadeGain (i);
                        state.gainEnd = (float) juce::jmax (0, bundle.voiceManager.getStealFadeSamplesRemaining (i) - chunk)
                                        / (float) bazalt::engine::VoiceManager::stealFadeSamples;
                    }
                }
            }

            processPlanRange (plan, startSample, chunk);

            const auto* out = plan->blockBuffers[(size_t) plan->finalOutputBufferIndex].getBlock().getChannelPointer (0);
            const auto* outRight = rightOutputOf (*plan, out);
            auto* scratch = renderScratchBuffer.getWritePointer (0) + startSample;
            auto* scratchRight = renderScratchBuffer.getWritePointer (1) + startSample;
            for (int i = 0; i < chunk; ++i)
            {
                scratch[i] = out[i];
                scratchRight[i] = outRight[i];
            }

            // After the range: finish steals, release instances gone silent.
            for (int g = 0; g < (int) plan->instanceGroups.size(); ++g)
            {
                auto& group = *plan->instanceGroups[(size_t) g];
                if (group.bundleSlot < 0)
                    continue;
                auto& bundle = originBundles[(size_t) group.bundleSlot];
                if (! bundle.active || bundle.swarmPopulationSize.load (std::memory_order_relaxed) >= 0)
                    continue;

                int active = 0;
                for (int i = 0; i < group.count; ++i)
                {
                    const auto stage = bundle.voiceManager.getStage (i);
                    if (stage == VoiceStage::Idle)
                        continue;
                    ++active;
                    if (stage == VoiceStage::Stealing)
                    {
                        if (bundle.voiceManager.advanceStealFade (i, chunk))
                        {
                            const auto pending = bundle.voiceManager.getPendingNoteOn (i);
                            startInstance (*plan, g, i, pending.pitch, pending.velocity, pending.noteId == internalTriggerNoteId);
                            bundle.voiceManager.completeSteal (i);
                        }
                    }
                    else if (stage == VoiceStage::Releasing)
                    {
                        if (bundle.voiceManager.updateSilenceAndCheckFinished (i, group.instances[(size_t) i].peak, chunk,
                                                                                silenceThresholdLinear, silenceHoldTimeSamples))
                            bundle.voiceManager.voiceFinished (i);
                    }
                }
                bundle.activeVoiceCountThisBlock = active;
            }

            startSample += chunk;
            numSamples -= chunk;
        }
    }

    void BazaltAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
    {
        juce::ScopedNoDenormals noDenormals;

        // TEMPORARY diagnostic — see the member declarations' own comment.
        // A plain atomic increment, relaxed ordering — audio-thread-safe,
        // no allocation, no lock; the actual file write happens later, on
        // the message thread's own timerCallback().
        processBlockCallCount.fetch_add (1, std::memory_order_relaxed);

        const auto numSamples = buffer.getNumSamples();

        // M21: snapshot the host's input BEFORE clearing anything. This used to
        // clear every channel first, which wiped the main input (shared with the
        // output) and the sidechain aux inputs before anything could read them -
        // the sidechain meters and passthrough never saw a sample of real audio.
        captureHostInput (buffer, numSamples);
        beginTransportForBlock (numSamples);

        // Clear only the OUTPUT channels. The aux inputs live beyond them in the
        // host buffer and captureHostInput already copied them at the end.
        for (int ch = 0; ch < getTotalNumOutputChannels() && ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, numSamples);

        // The one plan, fetched exactly once per process() call (PlanSwapper's
        // contract); MIDI handling and every range below read this pointer.
        planThisBlock = globalPlanSwapper.getCurrentPlanForAudioThread();

        // Exactly once per block - see advanceSmoothers()'s own doc comment.
        macroParameters.advanceSmoothers (numSamples);
        liveParameterEdits.advance (numSamples);
        bazalt::engine::ExecutionPlan* plans[1] = { planThisBlock };
        macroParameters.applyToPlans (plans, 1, numSamples);
        liveParameterEdits.applyToPlans (plans, 1);
        liveParameterEdits.retireFinished();

        // Previews show each group's most recently played instance.
        if (planThisBlock != nullptr)
            for (auto& group : planThisBlock->instanceGroups)
                if (group->bundleSlot >= 0)
                    group->previewInstance.store (juce::jmax (0, originBundles[(size_t) group->bundleSlot].voiceManager.getMostRecentlyTriggeredVoice()),
                                                  std::memory_order_relaxed);

        // The block is split at every MIDI event, so a note starts on its sample.
        int previousSample = 0;

        for (const auto metadata : midiMessages)
        {
            const auto eventSample = metadata.samplePosition;

            if (eventSample > previousSample)
                renderRange (planThisBlock, previousSample, eventSample - previousSample);

            handleMidiEvent (metadata.getMessage());
            previousSample = eventSample;
        }

        if (previousSample < numSamples)
            renderRange (planThisBlock, previousSample, numSamples - previousSample);

        writeOutput (buffer, numSamples);

        updateAuxLevels (numSamples);

        if (auto* mainTap = tapPointers[0])
            mainTap->push (buffer.getReadPointer (0), buffer.getNumSamples());

        outputGuard.process (buffer); // first: NaN/Inf -> silence, so the limiter's own gain-reduction envelope never gets poisoned
        if (outputLimiterEnabled)
            outputLimiter.process (buffer); // then: a real safety ceiling (NanGuard alone never capped loud-but-finite signals)
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
        auto doc = bazalt::engine::PatchDocument::fromNodeGraph (graphEditController.getGraphForSaving());

        // wiki/plans/UtilMacro.md: macroMappings is no longer a persisted
        // field (schema v7) — it's derived from the graph's own util.macro
        // nodes on every recompile, so fromNodeGraph()'s node/connection
        // content already carries everything needed to reconstruct it on
        // load. Only the raw per-slot values still need to be saved.
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

        // Mappings themselves aren't loaded here — setGraph() above already
        // ran a real recompileAndPublish(), which derived them fresh from
        // the loaded graph's own util.macro nodes (GraphEditController.cpp)
        // and called setMacroMappings(). Only the raw per-slot values,
        // which the graph can't carry, are applied here.
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
