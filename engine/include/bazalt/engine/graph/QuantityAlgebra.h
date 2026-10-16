#pragma once

#include "bazalt/engine/graph/PortDescriptor.h"
#include <array>
#include <cstddef>

namespace bazalt::engine
{
    /** What a sum or a product of quantities means, for the math nodes that take
        on the meaning of their inputs (Add, Multiply).

        - A **unit** (Frequency, Pitch, Time, Phase) survives arithmetic with plain
          numbers: 440 Hz × 1.02 is Hz, a pitch + a wobble is a pitch, and an LFO
          (an Audio-quantity waveform) times a Hz depth is a Hz deviation.
        - A **scale** (Gain, Ratio, Count) is a unit in a sum but only a number
          in a product (a gain times a waveform is still a waveform).
        - Two different units, or a unit times itself (Hz × Hz), mean nothing a
          port could name: Dimensionless, the universal wildcard.
        - With no unit or scale at all: Audio if any input is Audio (a waveform
          scaled is still a waveform), else the unanimous normalised quantity. */
    namespace quantityAlgebra
    {
        inline bool isUnit (Quantity q) noexcept
        {
            return q == Quantity::Frequency || q == Quantity::Pitch || q == Quantity::Time || q == Quantity::Phase;
        }

        inline bool isScale (Quantity q) noexcept
        {
            return q == Quantity::Gain || q == Quantity::Ratio || q == Quantity::Count;
        }

        enum class Operation { Sum, Product };

        template <size_t N>
        Quantity combine (Operation operation, const std::array<Quantity, N>& quantities, const std::array<bool, N>& present) noexcept
        {
            auto anyAudio = false;
            int units = 0;
            auto unit = Quantity::Dimensionless;
            auto unitsDisagree = false;
            auto other = Quantity::Dimensionless; // scales (in a product) and normalised values
            auto anyOther = false, othersDisagree = false;

            for (size_t i = 0; i < N; ++i)
            {
                if (! present[i])
                    continue;
                const auto q = quantities[i];
                anyAudio = anyAudio || q == Quantity::Audio;
                if (q == Quantity::Dimensionless || q == Quantity::Boolean || q == Quantity::Audio)
                    continue;

                const auto countsAsUnit = isUnit (q) || (operation == Operation::Sum && isScale (q));
                if (countsAsUnit)
                {
                    if (units > 0 && q != unit)
                        unitsDisagree = true;
                    unit = q;
                    ++units;
                    continue;
                }
                if (anyOther && q != other)
                    othersDisagree = true;
                other = q;
                anyOther = true;
            }

            if (units > 0)
            {
                if (unitsDisagree)
                    return Quantity::Dimensionless;
                // Hz + Hz is Hz; Hz × Hz is not.
                return operation == Operation::Sum || units == 1 ? unit : Quantity::Dimensionless;
            }
            if (anyAudio)
                return Quantity::Audio;
            return anyOther && ! othersDisagree ? other : Quantity::Dimensionless;
        }
    }
}
