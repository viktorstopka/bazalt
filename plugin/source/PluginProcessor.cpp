#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/patch/PatchSerializer.h"
#include "bazalt/engine/nodes/InstanceMixNode.h"
#include "bazalt/engine/nodes/IoNoteInNode.h"
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

        telemetryHub.prepare (8192, 16384);

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
        if (auto* noteIn = dynamic_cast<bazalt::engine::nodes::IoNoteInNode*> (plan->getNodeById ("noteIn")))
            noteIn->injectNoteOn (pitch, velocity);
    }

    void BazaltAudioProcessor::repointVoiceDomainTaps (int newVoiceIndex, const VoicePlanPtrs& voicePlans) noexcept
    {
        if (newVoiceIndex == lastPointedVoiceForTaps)
            return;

        const auto oldVoiceIndex = lastPointedVoiceForTaps;
        lastPointedVoiceForTaps = newVoiceIndex;

        for (auto& slot : voiceDomainTapSlots)
        {
            if (! slot.active.load (std::memory_order_acquire))
                continue;

            if (oldVoiceIndex >= 0 && voicePlans[(size_t) oldVoiceIndex] != nullptr)
                voicePlans[(size_t) oldVoiceIndex]->setTapForBufferIndex (slot.bufferIndex, nullptr);

            if (voicePlans[(size_t) newVoiceIndex] != nullptr)
                voicePlans[(size_t) newVoiceIndex]->setTapForBufferIndex (slot.bufferIndex, slot.tap);
        }
    }

    void BazaltAudioProcessor::handleMidiEvent (const juce::MidiMessage& message, const VoicePlanPtrs& voicePlans)
    {
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
            repointVoiceDomainTaps (voiceIndex, voicePlans);

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

            if (voiceIndex >= 0 && voicePlans[(size_t) voiceIndex] != nullptr)
                if (auto* noteIn = dynamic_cast<bazalt::engine::nodes::IoNoteInNode*> (voicePlans[(size_t) voiceIndex]->getNodeById ("noteIn")))
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
                if (plan != nullptr)
                    if (auto* noteIn = dynamic_cast<bazalt::engine::nodes::IoNoteInNode*> (plan->getNodeById ("noteIn")))
                        noteIn->injectPitchBend (bendSemitones);
        }
    }

    bool BazaltAudioProcessor::subscribeVisualizationTap (const juce::String& nodeId, const juce::String& portId)
    {
        // Global domain checked first: a node only ever lives in one domain
        // (DomainSplitter's own invariant), and the global plan — when one
        // exists — is the simpler case (exactly one plan, never re-pointed).
        if (hasGlobalDomain.load (std::memory_order_acquire))
        {
            if (auto* plan = globalPlanSwapper.peekCurrentPlan())
            {
                const auto nodeIt = plan->outputBufferIndexByNodeAndPort.find (nodeId);
                if (nodeIt != plan->outputBufferIndexByNodeAndPort.end())
                {
                    const auto portIt = nodeIt->second.find (portId);
                    if (portIt != nodeIt->second.end())
                    {
                        auto* tap = telemetryHub.subscribeTap ("node:" + nodeId + ":" + portId);
                        plan->setTapForBufferIndex (portIt->second, tap);
                        return true;
                    }
                }
            }
        }

        // Voice domain: every voice's plan shares the same topology (all
        // compiled from the same NodeGraph), so voice 0's plan is only ever
        // used here to resolve the buffer index — the actual tap gets
        // pointed at whichever voice is currently most-recently-triggered
        // (or voice 0 if no note has ever been played yet).
        auto* representativePlan = voicePlanSwappers[0].peekCurrentPlan();
        if (representativePlan == nullptr)
            return false;

        const auto nodeIt = representativePlan->outputBufferIndexByNodeAndPort.find (nodeId);
        if (nodeIt == representativePlan->outputBufferIndexByNodeAndPort.end())
            return false;
        const auto portIt = nodeIt->second.find (portId);
        if (portIt == nodeIt->second.end())
            return false;

        auto* freeSlot = static_cast<VoiceDomainTapSlot*> (nullptr);
        for (auto& slot : voiceDomainTapSlots)
        {
            if (! slot.active.load (std::memory_order_acquire))
            {
                freeSlot = &slot;
                break;
            }
        }
        if (freeSlot == nullptr)
            return false; // all maxVoiceDomainTaps slots in use — bounded, matches TelemetryHub's own cap

        auto* tap = telemetryHub.subscribeTap ("node:" + nodeId + ":" + portId);
        freeSlot->nodeId = nodeId;
        freeSlot->portId = portId;
        freeSlot->bufferIndex = portIt->second;
        freeSlot->tap = tap;
        freeSlot->active.store (true, std::memory_order_release);

        const auto currentVoice = voiceManager.getMostRecentlyTriggeredVoice();
        const auto voiceToPoint = currentVoice >= 0 ? currentVoice : 0;
        if (auto* plan = voicePlanSwappers[(size_t) voiceToPoint].peekCurrentPlan())
            plan->setTapForBufferIndex (portIt->second, tap);

        return true;
    }

    void BazaltAudioProcessor::unsubscribeVisualizationTap (const juce::String& nodeId, const juce::String& portId)
    {
        if (auto* plan = globalPlanSwapper.peekCurrentPlan())
        {
            const auto nodeIt = plan->outputBufferIndexByNodeAndPort.find (nodeId);
            if (nodeIt != plan->outputBufferIndexByNodeAndPort.end())
            {
                const auto portIt = nodeIt->second.find (portId);
                if (portIt != nodeIt->second.end())
                    plan->setTapForBufferIndex (portIt->second, nullptr);
            }
        }

        for (auto& slot : voiceDomainTapSlots)
        {
            if (! slot.active.load (std::memory_order_acquire) || slot.nodeId != nodeId || slot.portId != portId)
                continue;

            const auto currentVoice = voiceManager.getMostRecentlyTriggeredVoice();
            const auto voiceToPoint = currentVoice >= 0 ? currentVoice : 0;
            if (auto* plan = voicePlanSwappers[(size_t) voiceToPoint].peekCurrentPlan())
                plan->setTapForBufferIndex (slot.bufferIndex, nullptr);

            slot.active.store (false, std::memory_order_release);
            break;
        }

        telemetryHub.unsubscribeTap ("node:" + nodeId + ":" + portId);
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

                plan->process (numSamples);
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
                        plan->process (remainingSamples);
                        const auto* newVoiceOut = plan->blockBuffers[(size_t) plan->finalOutputBufferIndex].getBlock().getChannelPointer (0);
                        for (int i = 0; i < remainingSamples; ++i)
                            sum[fadingSamples + i] += newVoiceOut[i];
                    }
                }

                continue;
            }

            // Active or Releasing.
            plan->process (numSamples);
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

        if (hasGlobalDomain.load (std::memory_order_acquire))
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
                        globalPlan->process (numSamples);
                        finalMono = globalPlan->blockBuffers[(size_t) globalPlan->finalOutputBufferIndex]
                                        .getBlock()
                                        .getChannelPointer (0);
                    }
                }
            }
        }

        auto* left = output.getWritePointer (0);
        auto* right = output.getNumChannels() > 1 ? output.getWritePointer (1) : left;

        for (int i = 0; i < numSamples; ++i)
        {
            left[i] += finalMono[i];
            right[i] += finalMono[i];
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

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, numSamples);

        instanceMixScratchBuffer.clear (0, numSamples);

        // Fetched exactly once per process() call, per PlanSwapper's own
        // contract (PlanSwapper.h) — handleMidiEvent/renderVoiceRange below
        // both read from this same cached array rather than re-querying
        // the swappers mid-block.
        VoicePlanPtrs voicePlanPtrs {};
        for (int i = 0; i < numVoices; ++i)
            voicePlanPtrs[(size_t) i] = voicePlanSwappers[(size_t) i].getCurrentPlanForAudioThread();

        macroParameters.applyToPlans (voicePlanPtrs.data(), numVoices, numSamples);

        int previousSample = 0;

        for (const auto metadata : midiMessages)
        {
            const auto eventSample = metadata.samplePosition;

            if (eventSample > previousSample)
                renderVoiceRange (previousSample, eventSample - previousSample, voicePlanPtrs);

            handleMidiEvent (metadata.getMessage(), voicePlanPtrs);
            previousSample = eventSample;
        }

        if (previousSample < numSamples)
            renderVoiceRange (previousSample, numSamples - previousSample, voicePlanPtrs);

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
