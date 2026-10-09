#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/nodes/NoiseBurstNode.h"
#include "bazalt/engine/nodes/IoNoteInNode.h"

#include <algorithm>
#include <functional>

// M2: renders both hardcoded proof graphs (ARCHITECTURE.md §3.4) headlessly
// via the real NodeGraph -> GraphCompiler -> ExecutionPlan path, proving the
// engine-only build works end to end with no plugin/UI dependency
// (docs/MILESTONES.md, M2 exit criteria). Notes are triggered by calling
// straight into the compiled node instances (AdsrNode::noteOn/noteOff,
// NoiseBurstNode::trigger) — full patch + MIDI file input replaces this once
// the real note/voice data model is wired to MIDI input (M3).
namespace
{
    bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> fileStream (file.createOutputStream());

        if (fileStream == nullptr)
        {
            juce::Logger::writeToLog ("bazalt-render-cli: failed to open output file: " + file.getFullPathName());
            return false;
        }

        juce::WavAudioFormat wavFormat;
        const auto writerOptions = juce::AudioFormatWriterOptions {}
                                        .withSampleRate (sampleRate)
                                        .withNumChannels (buffer.getNumChannels())
                                        .withBitsPerSample (24);

        auto writer = wavFormat.createWriterFor (fileStream, writerOptions);

        if (writer == nullptr)
        {
            juce::Logger::writeToLog ("bazalt-render-cli: failed to create WAV writer for " + file.getFullPathName());
            return false;
        }

        if (! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
        {
            juce::Logger::writeToLog ("bazalt-render-cli: failed to write samples to " + file.getFullPathName());
            return false;
        }

        juce::Logger::writeToLog ("bazalt-render-cli: wrote " + juce::String (buffer.getNumSamples())
                                   + " samples to " + file.getFullPathName());
        return true;
    }

    juce::AudioBuffer<float> renderPlan (bazalt::engine::ExecutionPlan& plan, int totalSamples, int blockSize,
                                          const std::function<void (int)>& onBlockStart)
    {
        juce::AudioBuffer<float> output (1, totalSamples);
        int rendered = 0;

        while (rendered < totalSamples)
        {
            onBlockStart (rendered);

            const auto thisBlock = std::min (blockSize, totalSamples - rendered);
            plan.process (thisBlock);

            const auto* src = plan.blockBuffers[(size_t) plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);
            output.copyFrom (0, rendered, src, thisBlock);

            rendered += thisBlock;
        }

        return output;
    }

    bool renderVoiceGraph (const juce::File& outputFile, double sampleRate, int blockSize)
    {
        using namespace bazalt::engine;

        auto graph = buildVoiceProofGraph();
        auto factory = buildDefaultNodeFactory();
        auto result = GraphCompiler::compile (graph, factory, { sampleRate, blockSize }, 1);

        if (! result.success)
        {
            juce::Logger::writeToLog ("bazalt-render-cli: voice graph compile failed: " + result.errorMessage);
            return false;
        }

        auto& plan = result.plan;

        // M18 (ADR-0024): "osc"/"env" no longer take frequency/gate
        // directly — instance.allocate.voice's real pitch/gate ports drive them
        // now, fed by io.noteIn, exactly like PluginProcessor.
        auto* noteIn = dynamic_cast<nodes::IoNoteInNode*> (plan.getNodeById ("noteIn"));
        jassert (noteIn != nullptr);

        const auto totalSamples = (int) (sampleRate * 1.6);
        const auto noteOffSample = (int) (sampleRate * 1.0);
        bool noteOffSent = false;

        noteIn->injectNoteOn (57.0f, 1.0f); // A3, ~220Hz — matches the tone this tool has always rendered

        auto buffer = renderPlan (plan, totalSamples, blockSize, [&] (int sampleOffset)
        {
            if (! noteOffSent && sampleOffset >= noteOffSample)
            {
                noteIn->injectNoteOff();
                noteOffSent = true;
            }
        });

        return writeWav (outputFile, buffer, sampleRate);
    }

    bool renderKarplusStrongGraph (const juce::File& outputFile, double sampleRate, int blockSize)
    {
        using namespace bazalt::engine;

        auto graph = buildKarplusStrongProofGraph();
        auto factory = buildDefaultNodeFactory();
        auto result = GraphCompiler::compile (graph, factory, { sampleRate, blockSize }, 1);

        if (! result.success)
        {
            juce::Logger::writeToLog ("bazalt-render-cli: karplus-strong graph compile failed: " + result.errorMessage);
            return false;
        }

        auto& plan = result.plan;

        // ~220 Hz pluck: delay length in samples = sampleRate / frequency.
        plan.getNodeById ("delay")->setParameter ("time.delay.samples", (float) (sampleRate / 220.0));

        auto* excite = dynamic_cast<nodes::NoiseBurstNode*> (plan.getNodeById ("excite"));
        jassert (excite != nullptr);
        excite->trigger ((int) (sampleRate * 0.005)); // 5ms pluck

        const auto totalSamples = (int) (sampleRate * 2.0);
        auto buffer = renderPlan (plan, totalSamples, blockSize, [] (int) {});

        return writeWav (outputFile, buffer, sampleRate);
    }
}

int main (int argc, char* argv[])
{
    const juce::File outputDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                           : juce::File::getCurrentWorkingDirectory();
    outputDir.createDirectory();

    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    const auto voiceOk = renderVoiceGraph (outputDir.getChildFile ("voice.wav"), sampleRate, blockSize);
    const auto karplusOk = renderKarplusStrongGraph (outputDir.getChildFile ("karplus-strong.wav"), sampleRate, blockSize);

    return (voiceOk && karplusOk) ? 0 : 1;
}
