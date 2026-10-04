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
        // -30 dB: numerically measurable at the output (so a test can
        // inject a signal into an aux bus and confirm it arrived) without
        // being a meaningful part of the mix. Real sidechain-driven DSP
        // (ducking, modulation) is a future feature once the graph model
        // has aux-typed ports — this is the interim "proof" the milestone
        // asks for, not the final behaviour.
        constexpr float sidechainPassthroughGain = 0.0316f;

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

        for (int i = 0; i < numAuxBuses; ++i)
            layout = layout.withInput ("Sidechain " + juce::String (i + 1), juce::AudioChannelSet::stereo(), false);

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
        for (auto& bundle : originBundles)
            for (auto& swapper : bundle.voicePlanSwappers)
                swapper.reclaim();

        globalPlanSwapper.reclaim();

        // TEMPORARY diagnostic — see the member declarations' own comment.
        const auto current = processBlockCallCount.load (std::memory_order_relaxed);
        if (current != lastLoggedProcessBlockCallCount)
        {
            logDiagnostic ("processBlock total calls so far: " + juce::String ((juce::int64) current)
                            + " (sampleRate=" + juce::String (currentSampleRate)
                            + ", blockSize=" + juce::String (currentBlockSize) + ")");
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
            return;

        hasBeenPrepared = true;
        currentSampleRate = sampleRate;
        currentBlockSize = samplesPerBlock;

        outputLimiter.prepare (sampleRate);

        monoRenderScratchBuffer.setSize (2, samplesPerBlock);

        for (auto& bundle : originBundles)
        {
            bundle.instanceMixScratchBuffer.setSize (2, samplesPerBlock);
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

        // M18 (ADR-0024): the one remaining direct C++ poke for real host
        // MIDI — everything downstream (instance.allocate.voice's outputs
        // into osc's "pitch" and env's "gate") is now real graph wiring,
        // not further pokes.
        if (auto* noteIn = findNoteIn (plan))
            noteIn->injectNoteOn (pitch, velocity);
    }

    void BazaltAudioProcessor::triggerVoiceNoteViaAllocator (bazalt::engine::ExecutionPlan* plan, const juce::String& originNodeId, float pitch, float velocity) noexcept
    {
        if (plan == nullptr)
            return;

        plan->reset();

        // DomainRedesign.md Batch 2: pokes the allocator DIRECTLY instead of
        // via io.noteIn — this origin may have no io.noteIn at all (a purely
        // internally-sequenced region), and the allocator's own spawnInstance()
        // already does everything a real trigger needs (gate/pitch/velocity,
        // instanceIndex, a fresh random1/random2 draw). spawnInstance(), not
        // the concrete InstanceVoiceNode::noteOn() — Domain Extensions batch,
        // this call site serves Voice/Swarm-transient/Trigger alike now.
        if (auto* allocator = findAllocatorNode (plan, originNodeId))
            allocator->spawnInstance (pitch, velocity);
    }

    bazalt::engine::nodes::IoNoteInNode* BazaltAudioProcessor::findNoteIn (bazalt::engine::ExecutionPlan* plan) const noexcept
    {
        // 09-28-InstanceAllocator: resolved by type at compile time
        // (ExecutionPlan::noteInNodeId's own comment has the real bug this
        // fixes) — plan->noteInNodeId is a prebuilt String read here, never
        // a literal (CLAUDE.md's own "never call getNodeById on a literal
        // on the audio thread" rule).
        return plan == nullptr ? nullptr : dynamic_cast<bazalt::engine::nodes::IoNoteInNode*> (plan->getNodeById (plan->noteInNodeId));
    }

    bazalt::engine::nodes::InstanceOriginNode* BazaltAudioProcessor::findAllocatorNode (bazalt::engine::ExecutionPlan* plan, const juce::String& originNodeId) const noexcept
    {
        // Domain Extensions batch: InstanceOriginNode*, not the concrete
        // InstanceVoiceNode* this used to return — see this method's own
        // header doc comment.
        return plan == nullptr ? nullptr : dynamic_cast<bazalt::engine::nodes::InstanceOriginNode*> (plan->getNodeById (originNodeId));
    }

    void BazaltAudioProcessor::pointVoiceTapsAtCurrentVoice (OriginBundle& bundle, const VoicePlanPtrs& voicePlans) noexcept
    {
        const auto mostRecent = bundle.voiceManager.getMostRecentlyTriggeredVoice();
        const auto target = mostRecent >= 0 ? mostRecent : 0;

        for (int i = 0; i < numVoices; ++i)
        {
            auto* plan = voicePlans[(size_t) i];
            const auto shouldBeOn = i == target;

            if (plan != nullptr && plan->arePreviewTapsEnabled() != shouldBeOn)
                plan->setPreviewTapsEnabled (shouldBeOn);
        }
    }

    void BazaltAudioProcessor::handleMidiEvent (const juce::MidiMessage& message, const std::array<VoicePlanPtrs, maxOrigins>& originVoicePlanPtrs)
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

        // A mono graph (no active origin) has no voices: nothing below -
        // allocating one, poking its note-in - applies.
        if (monoOnlyGraph.load (std::memory_order_acquire))
            return;

        // Real host MIDI broadcasts identically to EVERY active origin —
        // an internally-sequenced origin simply has no io.noteIn to receive
        // it, and a real MIDI-driven one gets a voice allocated in its OWN
        // VoiceManager, exactly as a single-origin graph always did.
        if (message.isNoteOn())
        {
            const auto noteId = (bazalt::engine::VoiceManager::NoteId) ((message.getChannel() << 8) | message.getNoteNumber());
            const auto pitch = (float) message.getNoteNumber();
            const auto velocity = message.getFloatVelocity();

            for (int b = 0; b < maxOrigins; ++b)
            {
                auto& bundle = originBundles[(size_t) b];
                if (! bundle.active)
                    continue;

                const auto& voicePlans = originVoicePlanPtrs[(size_t) b];
                const auto voiceIndex = bundle.voiceManager.noteOn (noteId);

                // M20: the preview tap follows whichever voice was just played,
                // whether it went straight to Active or is still fading out its
                // stolen predecessor — same "most recently triggered" value
                // VoiceManager itself now tracks.
                pointVoiceTapsAtCurrentVoice (bundle, voicePlans);

                // M17: a stolen voice defers its actual retrigger until its
                // fade-out completes (renderOriginVoiceRange) — DOMAINS.md §5's
                // "faded out over a ramp rather than cut". An idle-voice
                // allocation retriggers immediately, exactly as before.
                if (bundle.voiceManager.getStage (voiceIndex) == bazalt::engine::VoiceStage::Stealing)
                {
                    bundle.voiceManager.setPendingNoteOn (voiceIndex, { noteId, pitch, velocity });
                    continue;
                }

                triggerVoiceNote (voicePlans[(size_t) voiceIndex], pitch, velocity);
            }
        }
        else if (message.isNoteOff())
        {
            const auto noteId = (bazalt::engine::VoiceManager::NoteId) ((message.getChannel() << 8) | message.getNoteNumber());

            for (int b = 0; b < maxOrigins; ++b)
            {
                auto& bundle = originBundles[(size_t) b];
                if (! bundle.active)
                    continue;

                const auto voiceIndex = bundle.voiceManager.noteOff (noteId);

                if (voiceIndex >= 0)
                    if (auto* noteIn = findNoteIn (originVoicePlanPtrs[(size_t) b][(size_t) voiceIndex]))
                        noteIn->injectNoteOff();
            }
        }
        else if (message.isPitchWheel())
        {
            // M18 exit criterion: a pitch-bend render confirms continuous
            // Pitch needs no special-cased path — folded straight into
            // io.noteIn's continuous "pitch" output (ADR-0024), applied to
            // every voice of every active origin (pitch bend is channel-wide,
            // not per-note, so it isn't routed through VoiceManager's
            // single-target allocation the way note-on/off are).
            constexpr float pitchBendRangeSemitones = 2.0f; // standard default MIDI pitch bend range
            const auto normalized = ((float) message.getPitchWheelValue() - 8192.0f) / 8192.0f; // -1..~1
            const auto bendSemitones = normalized * pitchBendRangeSemitones;

            for (int b = 0; b < maxOrigins; ++b)
            {
                if (! originBundles[(size_t) b].active)
                    continue;

                for (auto* plan : originVoicePlanPtrs[(size_t) b])
                    if (auto* noteIn = findNoteIn (plan))
                        noteIn->injectPitchBend (bendSemitones);
            }
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

        // The global plan - when there is one - is checked first and is the
        // simpler case: one plan, one tap.
        if (globalPlan != nullptr)
        {
            // findTappableBufferIndex: an output port's own buffer, or - for a
            // node with no outputs, like view.scope - the buffer wired into
            // its input (ADR-0029).
            const auto bufferIndex = globalPlan->findTappableBufferIndex (subscription.nodeId, subscription.portId);
            if (bufferIndex >= 0)
                return globalPlan->addTapForBufferIndex (bufferIndex, subscription.tap);
        }

        // Every origin's own voice plans: a node normally lives in exactly one
        // origin, but an origin's own trigger source CAN be duplicated into
        // several origins (DomainRedesign.md's backward-inclusion rule) - a
        // tap on that specific node simply attaches to every plan that
        // resolves it, no different in spirit from attaching to several
        // voice plans of the SAME origin already.
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

        // Every ACTIVE origin's own plans, as raw pointers, concatenated in
        // bundle-slot order — the shape attachPreviewSubscription()/
        // planHasPort() want. An inactive bundle contributes nothing (its
        // swappers hold whatever plan they last did, but nothing keeps it
        // current, so it's simply skipped rather than peeked).
        std::vector<bazalt::engine::ExecutionPlan*> peekActiveOriginVoicePlans (
            std::array<BazaltAudioProcessor::OriginBundle, BazaltAudioProcessor::maxOrigins>& bundles)
        {
            std::vector<bazalt::engine::ExecutionPlan*> plans;
            plans.reserve (bundles.size() * (size_t) BazaltAudioProcessor::numVoices);
            for (auto& bundle : bundles)
            {
                if (! bundle.active)
                    continue;
                for (auto& swapper : bundle.voicePlanSwappers)
                    plans.push_back (swapper.peekCurrentPlan());
            }
            return plans;
        }
    }

    bool BazaltAudioProcessor::subscribeVisualizationTap (const juce::String& nodeId, const juce::String& portId, bazalt::engine::PreviewKind kind)
    {
        // The global swapper can still hold a plan from an earlier graph
        // shape, so it only counts while the graph actually has one.
        const auto hasGlobal = hasGlobalDomain.load (std::memory_order_acquire) || monoOnlyGraph.load (std::memory_order_acquire);
        auto* globalPlan = hasGlobal ? globalPlanSwapper.peekCurrentPlan() : nullptr;
        const auto voicePlans = peekActiveOriginVoicePlans (originBundles);

        // The port has to exist on the node. Whether anything is wired to it
        // yet is a separate question (below): a viewer is placed before it is
        // connected, and must come alive when it is.
        auto portExists = planHasPort (globalPlan, nodeId, portId);
        for (auto* plan : voicePlans)
            portExists = portExists || planHasPort (plan, nodeId, portId);

        if (! portExists)
        {
            logDiagnostic ("subscribeVisualizationTap: " + nodeId + ":" + portId + " - PORT DOES NOT EXIST in any plan, rejected");
            return false;
        }

        PreviewSubscription subscription { nodeId, portId, kind };
        const auto attached = attachPreviewSubscription (subscription, globalPlan, voicePlans); // unresolved is fine: pending
        logDiagnostic ("subscribeVisualizationTap: " + nodeId + ":" + portId + " kind=" + juce::String ((int) kind)
                        + " portExists=true attached=" + juce::String (attached ? "true" : "false")
                        + " (hasGlobal=" + juce::String (globalPlan != nullptr ? "true" : "false")
                        + ", activeVoicePlans=" + juce::String ((int) voicePlans.size()) + ")");

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

            for (auto* plan : peekActiveOriginVoicePlans (originBundles))
                if (plan != nullptr)
                    plan->removeTap (removedTap);
        }

        telemetryHub.unsubscribeTap ("node:" + nodeId + ":" + portId);
    }

    void BazaltAudioProcessor::applyPreviewSubscriptions (const std::vector<bazalt::engine::ExecutionPlan*>& voicePlans,
                                                          bazalt::engine::ExecutionPlan* globalPlan)
    {
        // TEMPORARY diagnostic — visualization investigation (direct
        // feedback: "scopes only ever work for one thing at a time,"
        // "glance will just often not work... for random"). This runs on
        // EVERY recompile (every graph edit) — logs, per currently-live
        // subscription, whether re-attaching it to the FRESHLY compiled
        // plan(s) succeeded or not, which is exactly the moment a
        // connect/disconnect-triggered domain change could silently drop
        // one.
        if (! previewSubscriptions.empty())
            logDiagnostic ("applyPreviewSubscriptions: re-attaching " + juce::String ((int) previewSubscriptions.size())
                            + " live subscription(s) (hasGlobal=" + juce::String (globalPlan != nullptr ? "true" : "false")
                            + ", activeVoicePlans=" + juce::String ((int) voicePlans.size()) + ")");

        for (auto& subscription : previewSubscriptions)
        {
            const auto attached = attachPreviewSubscription (subscription, globalPlan, voicePlans);
            logDiagnostic ("  re-attach " + subscription.nodeId + ":" + subscription.portId
                            + " -> " + juce::String (attached ? "OK" : "FAILED (port not found in any plan this compile)"));
        }

        // Point each active origin's own voice taps at ITS current voice
        // before the plans go live. A default-on plan would otherwise push
        // from every voice until the audio thread's next block start
        // corrects it. `voicePlans` is every active origin's 8 plans
        // concatenated in bundle-slot order (see peekActiveOriginVoicePlans);
        // slice it back apart per bundle here.
        size_t offset = 0;
        for (auto& bundle : originBundles)
        {
            if (! bundle.active)
                continue;

            VoicePlanPtrs pointers {};
            for (int i = 0; i < numVoices && offset + (size_t) i < voicePlans.size(); ++i)
                pointers[(size_t) i] = voicePlans[offset + (size_t) i];

            pointVoiceTapsAtCurrentVoice (bundle, pointers);
            offset += (size_t) numVoices;
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

    void BazaltAudioProcessor::renderOriginVoiceRange (OriginBundle& bundle, int startSample, int numSamples, const VoicePlanPtrs& voicePlans) noexcept
    {
        if (numSamples <= 0)
            return;

        // Domain Extensions batch: a swarmPopulation origin bypasses
        // VoiceManager's whole Idle/Active/Releasing/Stealing stage machine
        // entirely — "fixed count, always live" (archive_docs/DOMAINS.md
        // §3) means there is no spawn/release event to detect or dispatch
        // at all, unlike every other origin type the rest of this function
        // handles. See OriginBundle::swarmPopulationSize's own doc comment
        // for why this is a plain atomic int, not a VoiceManager concept.
        if (const auto populationSize = bundle.swarmPopulationSize.load (std::memory_order_relaxed); populationSize >= 0)
        {
            auto* sum = bundle.instanceMixScratchBuffer.getWritePointer (0) + startSample;
            auto* sumRight = bundle.instanceMixScratchBuffer.getWritePointer (1) + startSample;
            int activeCount = 0;
            const auto liveCount = juce::jmin (populationSize, numVoices);

            for (int voiceIndex = 0; voiceIndex < liveCount; ++voiceIndex)
            {
                auto* plan = voicePlans[(size_t) voiceIndex];
                if (plan == nullptr)
                    continue;

                processPlanRange (plan, startSample, numSamples);
                const auto* voiceOut = plan->blockBuffers[(size_t) plan->finalOutputBufferIndex].getBlock().getChannelPointer (0);
                const auto* voiceRight = rightOutputOf (*plan, voiceOut);
                for (int i = 0; i < numSamples; ++i)
                {
                    sum[i] += voiceOut[i];
                    sumRight[i] += voiceRight[i];
                }
                ++activeCount;
            }

            bundle.activeVoiceCountThisBlock = activeCount;
            return;
        }

        // DomainRedesign.md Batch 2 (InstanceVoiceNode::consumeSpawnEventsThisBlock's
        // own doc comment has the full story): detect + dispatch an internal
        // trigger from whatever happened on voice slot 0's own plan the LAST
        // time it ran — checked FIRST, before this range even runs, so a
        // trigger contributes audio starting THIS sub-range, the same
        // sample-accuracy real MIDI splitting already gives note-on/off.
        //
        // Real, found-live bug fixed here: spawnEventsThisBlock increments on
        // EVERY noteOn()/noteOff(), regardless of source (that's the whole
        // point — one counter for both directions) — including an ordinary
        // note arriving via real MIDI's own io.noteIn -> allocator.spawn
        // wiring, one render call after triggerVoiceNote() pokes it. Reading
        // it unconditionally misread that completely ordinary event as an
        // internal trigger and spawned a genuinely phantom extra voice at
        // the SAME pitch (caught by InitPatchTests.cpp's own polyphony RMS
        // check, a chord measuring QUIETER than expected — two same-pitch,
        // phase-offset oscillators summing destructively, not the
        // "obviously louder" bug this might suggest). Gated on this origin
        // having NO io.noteIn at all (voicePlans[0]->noteInNodeId, already
        // resolved once at compile time by type — ExecutionPlan.h's own
        // comment): a real MIDI-driven origin has no other way to reach
        // this counter at all once this guard is in place, and a purely
        // internally-sequenced one (this fix's actual target) never has an
        // io.noteIn to begin with.
        if (voicePlans[0] != nullptr && voicePlans[0]->noteInNodeId.isEmpty())
        {
            if (auto* allocator = findAllocatorNode (voicePlans[0], bundle.originNodeId))
            {
                const auto spawnCount = allocator->consumeSpawnEventsThisBlock();
                if (spawnCount > 0)
                {
                    if (allocator->getGate())
                    {
                        const auto pitch = allocator->getPitch();
                        const auto velocity = allocator->getVelocity();
                        const auto voiceIndex = bundle.voiceManager.noteOn (internalTriggerNoteId);

                        if (bundle.voiceManager.getStage (voiceIndex) == bazalt::engine::VoiceStage::Stealing)
                            bundle.voiceManager.setPendingNoteOn (voiceIndex, { internalTriggerNoteId, pitch, velocity });
                        else if (voiceIndex != 0) // slot 0's own state already reflects this trigger - nothing more to poke
                            triggerVoiceNoteViaAllocator (voicePlans[(size_t) voiceIndex], bundle.originNodeId, pitch, velocity);
                    }
                    else
                    {
                        const auto voiceIndex = bundle.voiceManager.noteOff (internalTriggerNoteId);
                        if (voiceIndex >= 0 && voiceIndex != 0)
                            if (auto* other = findAllocatorNode (voicePlans[(size_t) voiceIndex], bundle.originNodeId))
                                other->releaseInstance();
                    }
                }
            }
        }

        // Ensure slot 0 (the "template") runs THIS range even while
        // nominally Idle, purely so its own graph-wired trigger (if any)
        // can be detected next time — an Idle voice's plan otherwise never
        // runs at all (the loop below skips it), so nothing would ever
        // observe an internally-sequenced origin's own spawn signal.
        if (bundle.voiceManager.getStage (0) == bazalt::engine::VoiceStage::Idle && voicePlans[0] != nullptr)
            processPlanRange (voicePlans[0], startSample, numSamples);

        auto* sum = bundle.instanceMixScratchBuffer.getWritePointer (0) + startSample;
        auto* sumRight = bundle.instanceMixScratchBuffer.getWritePointer (1) + startSample;
        int activeCount = 0;

        for (int voiceIndex = 0; voiceIndex < numVoices; ++voiceIndex)
        {
            const auto stage = bundle.voiceManager.getStage (voiceIndex);
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
                const auto fadeSamplesRemainingBefore = bundle.voiceManager.getStealFadeSamplesRemaining (voiceIndex);
                const auto fadingSamples = juce::jmin (numSamples, fadeSamplesRemainingBefore);

                processPlanRange (plan, startSample, numSamples);
                const auto* voiceOut = plan->blockBuffers[(size_t) plan->finalOutputBufferIndex].getBlock().getChannelPointer (0);
                const auto* voiceRight = rightOutputOf (*plan, voiceOut);

                const auto startGain = bundle.voiceManager.getStealFadeGain (voiceIndex);
                const auto completed = bundle.voiceManager.advanceStealFade (voiceIndex, numSamples);
                const auto endGain = bundle.voiceManager.getStealFadeGain (voiceIndex);

                for (int i = 0; i < fadingSamples; ++i)
                {
                    const auto t = fadingSamples > 1 ? (float) i / (float) (fadingSamples - 1) : 1.0f;
                    const auto gain = startGain + (endGain - startGain) * t;
                    sum[i] += voiceOut[i] * gain;
                    sumRight[i] += voiceRight[i] * gain;
                }
                // Samples at/after fadingSamples: gain is 0 — nothing to add.

                ++activeCount;

                if (completed)
                {
                    const auto pending = bundle.voiceManager.getPendingNoteOn (voiceIndex);
                    if (pending.noteId == internalTriggerNoteId)
                        triggerVoiceNoteViaAllocator (plan, bundle.originNodeId, pending.pitch, pending.velocity);
                    else
                        triggerVoiceNote (plan, pending.pitch, pending.velocity);
                    bundle.voiceManager.completeSteal (voiceIndex);

                    // The new note starts right where the fade left off,
                    // within this SAME render call — sample-accurate to
                    // within one MIDI-event-boundary sub-range, not
                    // delayed to the next processBlock().
                    const auto remainingSamples = numSamples - fadingSamples;
                    if (remainingSamples > 0)
                    {
                        processPlanRange (plan, startSample + fadingSamples, remainingSamples);
                        const auto* newVoiceOut = plan->blockBuffers[(size_t) plan->finalOutputBufferIndex].getBlock().getChannelPointer (0);
                        const auto* newVoiceRight = rightOutputOf (*plan, newVoiceOut);
                        for (int i = 0; i < remainingSamples; ++i)
                        {
                            sum[fadingSamples + i] += newVoiceOut[i];
                            sumRight[fadingSamples + i] += newVoiceRight[i];
                        }
                    }
                }

                continue;
            }

            // Active or Releasing.
            processPlanRange (plan, startSample, numSamples);
            const auto* voiceOut = plan->blockBuffers[(size_t) plan->finalOutputBufferIndex].getBlock().getChannelPointer (0);
            const auto* voiceRight = rightOutputOf (*plan, voiceOut);

            for (int i = 0; i < numSamples; ++i)
            {
                sum[i] += voiceOut[i];
                sumRight[i] += voiceRight[i];
            }

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
                    peak = juce::jmax (peak, std::abs (voiceOut[i]), std::abs (voiceRight[i]));

                if (bundle.voiceManager.updateSilenceAndCheckFinished (voiceIndex, peak, numSamples,
                                                                        silenceThresholdLinear, silenceHoldTimeSamples))
                    bundle.voiceManager.voiceFinished (voiceIndex);
            }
        }

        bundle.activeVoiceCountThisBlock = activeCount;
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

    // A graph with no active origin (MultiplicityResolver's monoOnly, or a
    // real instance.sum with nothing to reduce) is one plan, run over every
    // range of the block whether or not any note is held. Its output lands
    // in monoRenderScratchBuffer, so finalizeInstanceMixIntoOutput needs no
    // special case beyond checking monoRenderedThisBlock first.
    void BazaltAudioProcessor::renderMonoRange (bazalt::engine::ExecutionPlan* plan, int startSample, int numSamples) noexcept
    {
        if (plan == nullptr || numSamples <= 0)
            return;

        processPlanRange (plan, startSample, numSamples);

        const auto* out = plan->blockBuffers[(size_t) plan->finalOutputBufferIndex].getBlock().getChannelPointer (0);
        const auto* outRight = rightOutputOf (*plan, out);
        auto* scratch = monoRenderScratchBuffer.getWritePointer (0) + startSample;
        auto* scratchRight = monoRenderScratchBuffer.getWritePointer (1) + startSample;

        for (int i = 0; i < numSamples; ++i)
        {
            scratch[i] = out[i];
            scratchRight[i] = outRight[i];
        }
    }

    // ARCHITECTURE.md/DOMAINS.md §2, generalized by DomainRedesign.md Batch 2
    // to N independent origins: with no real global content (every origin's
    // own instance.sum, if any, isn't what's audible), whichever origin's
    // own voice sum resolved as the designated output (outputOriginBundleIndex)
    // is copied straight to both channels, identical to pre-M7 behaviour for
    // the single-origin case. When the designated output resolved Scalar
    // instead (hasGlobalDomain), every active origin's own instance.sum node
    // (if it has one) is fed that origin's own voice sum before the ONE
    // shared global plan runs once, and ITS output — not any single origin's
    // raw sum — reaches the speakers.
    void BazaltAudioProcessor::finalizeInstanceMixIntoOutput (juce::AudioBuffer<float>& output, int numSamples,
                                                               bazalt::engine::ExecutionPlan* globalPlan) noexcept
    {
        const float* finalMono = nullptr;
        // Real stereo cable redesign (wiki/NODES.System.md §9): set below
        // only when the plan that actually reaches the speakers resolved a
        // real second (right) channel for its designated output — a graph
        // whose output stays plain Mono leaves this null, and the mono-
        // duplicate path at the bottom is unchanged, byte for byte.
        const float* finalRight = nullptr;

        if (monoRenderedThisBlock)
        {
            finalMono = monoRenderScratchBuffer.getReadPointer (0);
            finalRight = monoRenderScratchBuffer.getReadPointer (1);
        }
        else if (! hasGlobalDomain.load (std::memory_order_acquire))
        {
            // The designated output itself resolved Poly — this origin's own
            // voice sum IS the final output, with no global plan involved at
            // all (DomainRedesign.md §10.1's "voice sum is final output"
            // case, now keyed to a specific origin instead of assumed
            // singular). -1 means no active origin resolves this (shouldn't
            // happen while hasGlobalDomain is false and monoOnly isn't set,
            // but a graph-mode switch landing mid-block is exactly what
            // monoRenderedThisBlock's own comment already guards against —
            // treat it the same way: leave the output as whatever it already
            // is, silence after the earlier clear).
            const auto index = outputOriginBundleIndex.load (std::memory_order_acquire);
            if (index >= 0 && index < maxOrigins)
            {
                finalMono = originBundles[(size_t) index].instanceMixScratchBuffer.getReadPointer (0);
                finalRight = originBundles[(size_t) index].instanceMixScratchBuffer.getReadPointer (1);
            }
        }
        else if (globalPlan != nullptr)
        {
            // Feed every active origin's own instance.sum node (if it has
            // one) with that origin's own voice sum before running the
            // shared global plan once.
            for (int b = 0; b < maxOrigins; ++b)
            {
                auto& bundle = originBundles[(size_t) b];
                if (! bundle.active)
                    continue;

                const auto& sumNodeId = globalPlan->externalInputNodeIds[(size_t) b];
                if (sumNodeId.isEmpty())
                    continue; // 09-28-InstanceAllocator.1's independent-region case: this origin runs, unbridged

                auto* instanceSumNode = dynamic_cast<bazalt::engine::nodes::InstanceMixNode*> (globalPlan->getNodeById (sumNodeId));
                if (instanceSumNode == nullptr)
                    continue;

                if (instanceSumNode->getMode() == bazalt::engine::nodes::InstanceMixNode::Mode::Average
                    && bundle.activeVoiceCountThisBlock > 1)
                {
                    const auto scale = 1.0f / (float) bundle.activeVoiceCountThisBlock;
                    bundle.instanceMixScratchBuffer.applyGain (0, numSamples, scale);
                }

                instanceSumNode->setExternalBlock (bundle.instanceMixScratchBuffer.getReadPointer (0), numSamples,
                                                   bundle.instanceMixScratchBuffer.getReadPointer (1));
            }

            processPlanRange (globalPlan, 0, numSamples); // the global plan runs once over the whole block
            finalMono = globalPlan->blockBuffers[(size_t) globalPlan->finalOutputBufferIndex]
                            .getBlock()
                            .getChannelPointer (0);

            if (globalPlan->finalOutputBufferIndexRight >= 0)
                finalRight = globalPlan->blockBuffers[(size_t) globalPlan->finalOutputBufferIndexRight]
                                 .getBlock()
                                 .getChannelPointer (0);
        }

        if (finalMono == nullptr)
            return; // nothing resolved this block (see the outputOriginBundleIndex branch's own comment) - leave output at silence

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
        // host buffer and updateAuxLevelsAndPassthrough reads them at the end.
        for (int ch = 0; ch < getTotalNumOutputChannels() && ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, numSamples);

        // Fetched exactly once per process() call, per PlanSwapper's own
        // contract (PlanSwapper.h) — handleMidiEvent/renderOriginVoiceRange
        // below both read from this same cached data rather than
        // re-querying the swappers mid-block.
        // M21: a graph with no active origin is ONE plan in the global
        // swapper, run every block (see renderMonoRange). Fetched once, like
        // every origin's own voice plans, per PlanSwapper's contract.
        const auto monoOnly = monoOnlyGraph.load (std::memory_order_acquire);
        monoRenderedThisBlock = monoOnly;

        bazalt::engine::ExecutionPlan* monoPlan = nullptr;
        bazalt::engine::ExecutionPlan* globalPlanForThisBlock = nullptr;
        std::array<VoicePlanPtrs, maxOrigins> originVoicePlanPtrs {};

        // Exactly once per block, regardless of how many domains end up
        // calling applyToPlans() below — see advanceSmoothers()'s own doc
        // comment for why calling the old combined applyToPlans() once per
        // domain used to over-advance every macro's ~20ms ramp.
        macroParameters.advanceSmoothers (numSamples);
        liveParameterEdits.advance (numSamples);

        if (monoOnly)
        {
            monoPlan = globalPlanSwapper.getCurrentPlanForAudioThread();
            bazalt::engine::ExecutionPlan* monoPlans[1] = { monoPlan };
            macroParameters.applyToPlans (monoPlans, 1, numSamples);
            liveParameterEdits.applyToPlans (monoPlans, 1);
        }
        else
        {
            for (int b = 0; b < maxOrigins; ++b)
            {
                auto& bundle = originBundles[(size_t) b];
                bundle.instanceMixScratchBuffer.clear (0, numSamples); // both channels

                if (! bundle.active)
                    continue;

                auto& plans = originVoicePlanPtrs[(size_t) b];
                for (int i = 0; i < numVoices; ++i)
                    plans[(size_t) i] = bundle.voicePlanSwappers[(size_t) i].getCurrentPlanForAudioThread();

                macroParameters.applyToPlans (plans.data(), numVoices, numSamples);
                liveParameterEdits.applyToPlans (plans.data(), numVoices);

                // ADR-0029: switch the preview taps on for exactly one voice plan
                // per origin. Cheap (8 relaxed loads), and it also corrects the
                // fresh plans a publish just made live, so it is not enough to
                // rely on the note-on path alone.
                pointVoiceTapsAtCurrentVoice (bundle, plans);
            }

            // wiki/plans/UtilMacro.md Finding A: this used to be fetched only
            // inside finalizeInstanceMixIntoOutput, AFTER every applyToPlans
            // call above already ran — a macro (or any future mapping)
            // targeting global-domain content (an entirely ordinary patch
            // shape: per-voice synths feeding a shared master filter) never
            // saw its value applied. Fetched here instead, alongside every
            // other plan this block already fetches exactly once
            // (PlanSwapper.h's own audio-thread contract), then handed
            // straight to finalizeInstanceMixIntoOutput below rather than
            // letting it call getCurrentPlanForAudioThread() a second time.
            if (hasGlobalDomain.load (std::memory_order_acquire))
            {
                globalPlanForThisBlock = globalPlanSwapper.getCurrentPlanForAudioThread();
                bazalt::engine::ExecutionPlan* globalPlans[1] = { globalPlanForThisBlock };
                macroParameters.applyToPlans (globalPlans, 1, numSamples);
                liveParameterEdits.applyToPlans (globalPlans, 1);
            }
        }

        liveParameterEdits.retireFinished();

        auto renderRange = [&] (int start, int count)
        {
            if (monoOnly)
            {
                renderMonoRange (monoPlan, start, count);
                return;
            }

            for (int b = 0; b < maxOrigins; ++b)
            {
                auto& bundle = originBundles[(size_t) b];
                if (bundle.active)
                    renderOriginVoiceRange (bundle, start, count, originVoicePlanPtrs[(size_t) b]);
            }
        };

        int previousSample = 0;

        for (const auto metadata : midiMessages)
        {
            const auto eventSample = metadata.samplePosition;

            if (eventSample > previousSample)
                renderRange (previousSample, eventSample - previousSample);

            handleMidiEvent (metadata.getMessage(), originVoicePlanPtrs);
            previousSample = eventSample;
        }

        if (previousSample < numSamples)
            renderRange (previousSample, numSamples - previousSample);

        finalizeInstanceMixIntoOutput (buffer, numSamples, globalPlanForThisBlock);

        updateAuxLevelsAndPassthrough (buffer, numSamples);

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
        auto doc = bazalt::engine::PatchDocument::fromNodeGraph (graphEditController.getGraph());

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
