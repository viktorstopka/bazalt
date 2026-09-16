#include "bazalt/engine/PolyBlepOscillator.h"
#include <juce_core/juce_core.h>
#include <cmath>

namespace bazalt::engine
{
    void PolyBlepOscillator::prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
    }

    void PolyBlepOscillator::reset (double startPhase) noexcept
    {
        phase = startPhase;
        triangleState = 0.0;
    }

    void PolyBlepOscillator::setFrequency (float frequencyHz) noexcept
    {
        phaseIncrement = sampleRate > 0.0 ? (double) frequencyHz / sampleRate : 0.0;
    }

    double PolyBlepOscillator::polyBlep (double t, double dt) noexcept
    {
        if (dt <= 0.0)
            return 0.0;

        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0;
        }

        if (t > 1.0 - dt)
        {
            t = (t - 1.0) / dt;
            return t * t + t + t + 1.0;
        }

        return 0.0;
    }

    float PolyBlepOscillator::renderSaw() noexcept
    {
        double value = 2.0 * phase - 1.0;
        value -= polyBlep (phase, phaseIncrement);
        return (float) value;
    }

    float PolyBlepOscillator::renderSquare() noexcept
    {
        double value = phase < 0.5 ? 1.0 : -1.0;
        value += polyBlep (phase, phaseIncrement);
        value -= polyBlep (std::fmod (phase + 0.5, 1.0), phaseIncrement);
        return (float) value;
    }

    float PolyBlepOscillator::renderTriangle() noexcept
    {
        // Leaky-integrated bandlimited square — a standard, cheap
        // approximation. The leak coefficient tracks phaseIncrement, so
        // amplitude isn't perfectly frequency-independent; nothing in the
        // engine depends on exact triangle amplitude, only on it being
        // finite and bandlimited.
        double square = phase < 0.5 ? 1.0 : -1.0;
        square += polyBlep (phase, phaseIncrement);
        square -= polyBlep (std::fmod (phase + 0.5, 1.0), phaseIncrement);

        triangleState = phaseIncrement * square + (1.0 - phaseIncrement) * triangleState;
        return (float) (triangleState * 4.0);
    }

    float PolyBlepOscillator::renderNextSample() noexcept
    {
        float output = 0.0f;

        switch (waveform)
        {
            case OscillatorWaveform::Sine:
                output = (float) std::sin (juce::MathConstants<double>::twoPi * phase);
                break;
            case OscillatorWaveform::Saw:
                output = renderSaw();
                break;
            case OscillatorWaveform::Square:
                output = renderSquare();
                break;
            case OscillatorWaveform::Triangle:
                output = renderTriangle();
                break;
        }

        phase += phaseIncrement;
        if (phase >= 1.0)
            phase -= 1.0;

        return output;
    }
}
