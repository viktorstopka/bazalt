#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/patch/PatchSerializer.h"
#include "bazalt/engine/nodes/AdsrNode.h"
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
        analysisThread.stopThread (2000);
    }

    void BazaltAudioProcessor::setDefaultMacroMappings()
    {
        // Macro 1 -> oscillator shape, 2 -> filter cutoff, 3 -> filter
        // resonance, 4 -> envelope release (ARCHITECTURE.md §4.3's
        // "oscillator shape, filter cutoff/resonance, and envelope").
        // Macros 5-32 are left unmapped — inert automatable floats until
        // something needs them, per "arbitrary, cheap to change" sizing.
        macroMappings = { { 0, "osc", "osc.basic.shape", 0.0f, 3.0f },
                          { 1, "svf", "filter.svf.cutoff", 200.0f, 12000.0f },
                          { 2, "svf", "filter.svf.resonance", 0.3f, 4.0f },
                          { 3, "env", "env.adsr.release", 0.02f, 3.0f } };

        macroParameters.setMappings (macroMappings);
    }

    void BazaltAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        currentSampleRate = sampleRate;
        currentBlockSize = samplesPerBlock;

        voicePlans.clear();
        voicePlans.reserve ((size_t) numVoices);

        auto graph = bazalt::engine::buildVoiceProofGraph();

        for (int i = 0; i < numVoices; ++i)
        {
            auto result = bazalt::engine::GraphCompiler::compile (
                graph, nodeFactory, { sampleRate, samplesPerBlock }, (uint64_t) (i + 1));

            // The hardcoded voice graph always compiles — a failure here
            // is a real engine bug, not a runtime condition to recover
            // from gracefully.
            jassert (result.success);

            voicePlans.push_back (std::make_unique<bazalt::engine::ExecutionPlan> (std::move (result.plan)));
        }

        voiceManager.prepare (numVoices);
        macroParameters.prepare (sampleRate);

        // Always stop before re-preparing: prepareToPlay can be called
        // again (e.g. sample rate change) while the thread is running, and
        // touching its scratch buffers/sampleRate from the message thread
        // while it's mid-run() would be a data race.
        analysisThread.stopThread (2000);

        telemetryHub.prepare ({ "main", "aux1", "aux2", "aux3", "aux4" }, 8192, 16384);

        tapPointers[0] = telemetryHub.getTap ("main");
        for (int i = 0; i < numAuxBuses; ++i)
            tapPointers[(size_t) (i + 1)] = telemetryHub.getTap ("aux" + juce::String (i + 1));

        analysisThread.prepare (sampleRate);
        analysisThread.startThread();
    }

    void BazaltAudioProcessor::releaseResources()
    {
        analysisThread.stopThread (2000);
        voicePlans.clear();
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

    void BazaltAudioProcessor::handleMidiEvent (const juce::MidiMessage& message)
    {
        if (message.isNoteOn())
        {
            const auto noteId = (bazalt::engine::VoiceManager::NoteId) ((message.getChannel() << 8) | message.getNoteNumber());
            const auto voiceIndex = voiceManager.noteOn (noteId);

            auto& plan = *voicePlans[(size_t) voiceIndex];
            plan.reset(); // fresh phase/envelope/filter state for the (possibly stolen) voice

            const auto frequency = (float) juce::MidiMessage::getMidiNoteInHertz (message.getNoteNumber());

            if (auto* osc = plan.getNodeById ("osc"))
                osc->setParameter ("osc.basic.frequency", frequency);

            if (auto* adsr = dynamic_cast<bazalt::engine::nodes::AdsrNode*> (plan.getNodeById ("env")))
                adsr->noteOn();
        }
        else if (message.isNoteOff())
        {
            const auto noteId = (bazalt::engine::VoiceManager::NoteId) ((message.getChannel() << 8) | message.getNoteNumber());
            const auto voiceIndex = voiceManager.noteOff (noteId);

            if (voiceIndex >= 0)
                if (auto* adsr = dynamic_cast<bazalt::engine::nodes::AdsrNode*> (voicePlans[(size_t) voiceIndex]->getNodeById ("env")))
                    adsr->noteOff();
        }
    }

    void BazaltAudioProcessor::renderVoiceRange (juce::AudioBuffer<float>& output, int startSample, int numSamples) noexcept
    {
        if (numSamples <= 0)
            return;

        auto* left = output.getWritePointer (0) + startSample;
        auto* right = (output.getNumChannels() > 1 ? output.getWritePointer (1) : output.getWritePointer (0)) + startSample;

        for (int voiceIndex = 0; voiceIndex < numVoices; ++voiceIndex)
        {
            if (voiceManager.getStage (voiceIndex) == bazalt::engine::VoiceStage::Idle)
                continue;

            auto& plan = *voicePlans[(size_t) voiceIndex];
            plan.process (numSamples);

            const auto* voiceOut = plan.blockBuffers[(size_t) plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);

            for (int i = 0; i < numSamples; ++i)
            {
                left[i] += voiceOut[i];
                right[i] += voiceOut[i];
            }

            if (voiceManager.getStage (voiceIndex) == bazalt::engine::VoiceStage::Releasing)
            {
                auto* adsr = dynamic_cast<bazalt::engine::nodes::AdsrNode*> (plan.getNodeById ("env"));
                if (adsr != nullptr && ! adsr->isActive())
                    voiceManager.voiceFinished (voiceIndex);
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

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, buffer.getNumSamples());

        std::array<bazalt::engine::ExecutionPlan*, numVoices> planPtrs {};
        for (int i = 0; i < numVoices; ++i)
            planPtrs[(size_t) i] = voicePlans[(size_t) i].get();

        macroParameters.applyToPlans (planPtrs.data(), numVoices, buffer.getNumSamples());

        int previousSample = 0;

        for (const auto metadata : midiMessages)
        {
            const auto eventSample = metadata.samplePosition;

            if (eventSample > previousSample)
                renderVoiceRange (buffer, previousSample, eventSample - previousSample);

            handleMidiEvent (metadata.getMessage());
            previousSample = eventSample;
        }

        if (previousSample < buffer.getNumSamples())
            renderVoiceRange (buffer, previousSample, buffer.getNumSamples() - previousSample);

        updateAuxLevelsAndPassthrough (buffer, buffer.getNumSamples());

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
        auto doc = bazalt::engine::PatchDocument::fromNodeGraph (bazalt::engine::buildVoiceProofGraph());

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
