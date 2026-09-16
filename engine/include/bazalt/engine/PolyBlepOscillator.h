#pragma once

namespace bazalt::engine
{
    enum class OscillatorWaveform
    {
        Sine,
        Saw,
        Square,
        Triangle
    };

    /** Band-limited classic waveshapes via PolyBLEP discontinuity
        correction (ARCHITECTURE.md §5). Phase accumulation is
        double-precision so long playback never drifts audibly, while
        rendered samples are float32, per the engine's precision policy.

        Phase accumulation is deliberately factored out behind
        setFrequency()/renderNextSample() so a future mipmapped
        band-limited wavetable implementation can sit behind the same
        interface without touching callers.
    */
    class PolyBlepOscillator
    {
    public:
        void prepare (double newSampleRate) noexcept;
        void reset (double startPhase = 0.0) noexcept;

        void setFrequency (float frequencyHz) noexcept;
        void setWaveform (OscillatorWaveform newWaveform) noexcept { waveform = newWaveform; }
        OscillatorWaveform getWaveform() const noexcept { return waveform; }

        /** Advances the oscillator by one sample and returns it, nominally in [-1, 1]. */
        float renderNextSample() noexcept;

        void renderBlock (float* output, int numSamples) noexcept
        {
            for (int i = 0; i < numSamples; ++i)
                output[i] = renderNextSample();
        }

    private:
        float renderSaw() noexcept;
        float renderSquare() noexcept;
        float renderTriangle() noexcept;

        static double polyBlep (double t, double phaseIncrement) noexcept;

        double sampleRate = 44100.0;
        double phase = 0.0;
        double phaseIncrement = 0.0;
        double triangleState = 0.0;
        OscillatorWaveform waveform = OscillatorWaveform::Saw;
    };
}
