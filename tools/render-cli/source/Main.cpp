#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "bazalt/engine/PolyBlepOscillator.h"
#include "bazalt/engine/SvfFilter.h"

// M1: renders a fixed tone (saw through the TPT SVF) to WAV, headlessly.
// Proves the engine-only build path works end to end with no plugin/UI
// dependency (docs/MILESTONES.md, M1 exit criteria). Real patch + MIDI
// input replaces this fixed graph once NodeGraph/ExecutionPlan exist
// (M2/M3) — at that point this becomes `render-cli <patch.json> <in.mid>
// <out.wav>`.
int main (int argc, char* argv[])
{
    using namespace bazalt::engine;

    const juce::String outputPath = argc > 1 ? juce::String (argv[1])
                                              : juce::String ("bazalt-render-cli-output.wav");

    constexpr double sampleRate = 44100.0;
    constexpr double durationSeconds = 2.0;
    constexpr float frequencyHz = 440.0f;
    const int numSamples = (int) (sampleRate * durationSeconds);

    PolyBlepOscillator oscillator;
    oscillator.prepare (sampleRate);
    oscillator.setWaveform (OscillatorWaveform::Saw);
    oscillator.setFrequency (frequencyHz);

    SvfFilter filter;
    filter.prepare (sampleRate, (uint32_t) numSamples, 1);
    filter.setType (SvfFilterType::Lowpass);
    filter.setCutoffFrequency (2000.0f);
    filter.setResonance (0.7071f);

    juce::AudioBuffer<float> buffer (1, numSamples);
    auto* data = buffer.getWritePointer (0);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto raw = oscillator.renderNextSample();
        data[i] = filter.processSample (0, raw) * 0.5f;
    }

    juce::File outputFile (outputPath);
    outputFile.deleteFile();

    std::unique_ptr<juce::OutputStream> fileStream (outputFile.createOutputStream());

    if (fileStream == nullptr || ! static_cast<juce::FileOutputStream&> (*fileStream).openedOk())
    {
        juce::Logger::writeToLog ("bazalt-render-cli: failed to open output file: " + outputFile.getFullPathName());
        return 1;
    }

    juce::WavAudioFormat wavFormat;
    const auto writerOptions = juce::AudioFormatWriterOptions {}
                                    .withSampleRate (sampleRate)
                                    .withNumChannels (buffer.getNumChannels())
                                    .withBitsPerSample (24);

    auto writer = wavFormat.createWriterFor (fileStream, writerOptions);

    if (writer == nullptr)
    {
        juce::Logger::writeToLog ("bazalt-render-cli: failed to create WAV writer");
        return 1;
    }

    if (! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
    {
        juce::Logger::writeToLog ("bazalt-render-cli: failed to write samples");
        return 1;
    }

    juce::Logger::writeToLog ("bazalt-render-cli: wrote " + juce::String (numSamples)
                               + " samples to " + outputFile.getFullPathName());

    return 0;
}
