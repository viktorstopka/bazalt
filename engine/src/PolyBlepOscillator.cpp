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
        cycleCount = 0;
    }

    void PolyBlepOscillator::setFrequency (float frequencyHz) noexcept
    {
        phaseIncrement = sampleRate > 0.0 ? (double) frequencyHz / sampleRate : 0.0;
    }

    float PolyBlepOscillator::renderNextSample() noexcept
    {
        // One shared definition of every shape (BandLimited.h): the same
        // function the phase-locked preview evaluates.
        const auto output = (float) bandLimited::evaluate (waveform, phase, phaseIncrement);

        phase += phaseIncrement;
        if (phase >= 1.0)
        {
            phase -= 1.0;
            cycleCount = (cycleCount + 1) % cycleWrap;
        }

        return output;
    }
}
