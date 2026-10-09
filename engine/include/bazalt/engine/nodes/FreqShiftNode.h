#pragma once

#include "bazalt/engine/graph/Node.h"
#include <array>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "spectrum.freqShift" (wiki/plans/SoundPalette.md, Batch 4).
        Moves every partial up (or down) by the same number of Hz — unlike
        pitch shifting, which multiplies. Harmonic sounds turn inharmonic and
        bell-like; a few Hz gives slow phasing/barber-pole movement. Easy
        digitally, awkward in analog (the Bode shifter).

        A Hilbert transformer — two chains of four second-order allpasses
        (Olli Niemitalo's coefficients) whose outputs stay 90° apart from
        ~20 Hz to ~20 kHz at 44.1–48 kHz — gives the signal's analytic pair
        (I, Q); single-sideband modulation by a quadrature oscillator then
        shifts it — one sign of I·cos ± Q·sin goes up, the other down (which
        is which depends on Q's sign convention; the test pins it). `out` is
        shifted by `shift` Hz, `mirror` by −`shift` (both free at once).
        Per channel (each lane its own filters; one shared oscillator phase
        progression, so a stereo pair stays aligned). */
    class FreqShiftNode : public Node
    {
    public:
        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            reset();
        }
        void reset() override
        {
            pathA = {};
            pathB = {};
            delayedA = 0.0;
            phase = 0.0;
        }

        int getNumInputPorts() const noexcept override { return 3; }
        int getNumOutputPorts() const noexcept override { return 2; }
        juce::String getTitle() const override { return "Frequency Shift"; }
        juce::String getCategory() const override { return "Spectrum"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                perChannel ({ .id = "in", .type = SignalType::Signal, .quantity = Quantity::Audio }),
                PortDescriptor { .id = "spectrum.freqShift.shift", .type = SignalType::Signal, .label = "Shift", .unit = "Hz",
                                  .minValue = -5000.0f, .maxValue = 5000.0f, .defaultValue = 100.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Frequency, .polarity = Polarity::Bipolar,
                                  .softMin = -500.0f, .softMax = 500.0f },
                PortDescriptor { .id = "spectrum.freqShift.mix", .type = SignalType::Signal, .label = "Mix",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .hasFallbackWhenUnconnected = true,
                                  .quantity = Quantity::Unipolar },
            };
        }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                perChannel (PortDescriptor { .id = "out", .type = SignalType::Signal, .label = "Up", .isPrimaryOutput = true, .quantity = Quantity::Audio }),
                perChannel (PortDescriptor { .id = "mirror", .type = SignalType::Signal, .label = "Down", .quantity = Quantity::Audio }),
            };
        }
        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "spectrum.freqShift.shift")
                storedShift = value;
            else if (parameterId == "spectrum.freqShift.mix")
                storedMix = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto shift = (double) (std::isnan (inputs[1]) ? storedShift : juce::jlimit (-20000.0f, 20000.0f, inputs[1]));
            const auto mix = (double) (std::isnan (inputs[2]) ? storedMix : juce::jlimit (0.0f, 1.0f, inputs[2]));
            const auto x = (double) inputs[0];

            // Niemitalo's arrangement: the first chain's output delayed by one
            // sample is the in-phase part, the second chain's the quadrature.
            const auto i = delayedA;
            delayedA = pathA.process (x, coefficientsA);
            const auto q = pathB.process (x, coefficientsB);

            const auto c = std::cos (juce::MathConstants<double>::twoPi * phase);
            const auto s = std::sin (juce::MathConstants<double>::twoPi * phase);
            const auto up = i * c + q * s;
            const auto down = i * c - q * s;

            // The analytic pair lags the dry input by a few samples, so the
            // dry part of `mix` is taken from the in-phase path too.
            outputs[0] = (float) (i * (1.0 - mix) + up * mix);
            outputs[1] = (float) (i * (1.0 - mix) + down * mix);

            phase += sampleRate > 0.0 ? shift / sampleRate : 0.0;
            phase -= std::floor (phase);
        }

    private:
        static constexpr std::array<double, 4> coefficientsA { 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737 };
        static constexpr std::array<double, 4> coefficientsB { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 };

        struct AllpassChain
        {
            std::array<double, 4> x1 {}, x2 {}, y1 {}, y2 {};

            double process (double x, const std::array<double, 4>& a) noexcept
            {
                for (size_t k = 0; k < 4; ++k)
                {
                    const auto a2 = a[k] * a[k];
                    const auto y = a2 * (x + y2[k]) - x2[k];
                    x2[k] = x1[k];
                    x1[k] = x;
                    y2[k] = y1[k];
                    y1[k] = y;
                    x = y;
                }
                return x;
            }
        };

        double sampleRate = 48000.0, phase = 0.0, delayedA = 0.0;
        AllpassChain pathA, pathB;
        float storedShift = 100.0f, storedMix = 1.0f;
    };
}
