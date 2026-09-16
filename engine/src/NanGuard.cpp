#include "bazalt/engine/NanGuard.h"
#include <cmath>

namespace bazalt::engine
{
    void NanGuard::process (juce::AudioBuffer<float>& buffer) noexcept
    {
        bool foundFault = false;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* data = buffer.getWritePointer (channel);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                if (! std::isfinite (data[i]))
                {
                    data[i] = 0.0f;
                    foundFault = true;
                }
            }
        }

        if (foundFault)
            faultFlag.store (true, std::memory_order_relaxed);
    }
}
