#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "analysis.level" (wiki/plans/SoundPalette.md, Batch 1).
        How loud a signal is, as two Control outputs: `level` (linear
        amplitude) and `db`. RMS (what the ear calls loudness, a steady sine
        of peak 1 reads 0.707 / -3 dB) or peak (the largest excursion), with
        attack and release ballistics. Takes stereo — a mono cable broadcasts —
        and measures both channels together (mean power for RMS, the larger
        channel for peak), so it never needs a downmix in front of it. */
    class AnalysisLevelNode : public Node
    {
    public:
        enum class Mode { Rms, Peak };
        static constexpr float floorDb = -120.0f;

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            reset();
        }
        void reset() override { envelope = power = 0.0; }

        int getNumInputPorts() const noexcept override { return 3; }
        int getNumOutputPorts() const noexcept override { return 2; }
        int getNumInputChannels() const noexcept override { return 4; }
        juce::String getTitle() const override { return "Level"; }
        juce::String getCategory() const override { return "Analysis"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "in", .type = SignalType::Signal, .quantity = Quantity::Audio, .channels = Channels::Stereo },
                ValueTypes::timeMsPort ("analysis.level.attack", "Attack", 10.0f, 1000.0f),
                ValueTypes::timeMsPort ("analysis.level.release", "Release", 150.0f, 5000.0f),
            };
        }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "level", .type = SignalType::Signal, .label = "Level", .isPrimaryOutput = true,
                                  .minValue = 0.0f, .maxValue = 1.0f, .quantity = Quantity::Unipolar },
                PortDescriptor { .id = "db", .type = SignalType::Signal, .label = "dB", .unit = "dB",
                                  .minValue = floorDb, .maxValue = 12.0f, .quantity = Quantity::Gain },
            };
        }
        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "analysis.level.mode", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                            .displayName = "Mode", .isInteger = true, .kind = ValueKind::Enum,
                                            .enumOptions = { { "rms", "RMS" }, { "peak", "Peak" } } } };
        }
        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "analysis.level.mode")
                mode = value > 0.5f ? Mode::Peak : Mode::Rms;
            else if (parameterId == "analysis.level.attack")
                storedAttackMs = value;
            else if (parameterId == "analysis.level.release")
                storedReleaseMs = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto attackMs = std::isnan (inputs[2]) ? storedAttackMs : inputs[2];
            const auto releaseMs = std::isnan (inputs[3]) ? storedReleaseMs : inputs[3];

            // RMS: mean power over a fixed 50 ms window (symmetric — asymmetric
            // smoothing of power would read high), then the ballistics on the
            // level. Peak: the ballistics straight on the magnitude.
            const auto left = (double) inputs[0], right = (double) inputs[1];
            double target;
            if (mode == Mode::Rms)
            {
                power += rmsWindow.get (50.0f, sampleRate) * (0.5 * (left * left + right * right) - power);
                target = std::sqrt (power);
            }
            else
                target = std::max (std::abs (left), std::abs (right));

            const auto coefficient = target > envelope ? coefficientFor (attackMs, attackCache) : coefficientFor (releaseMs, releaseCache);
            envelope = target + coefficient * (envelope - target);
            const auto level = envelope;
            outputs[0] = (float) level;
            outputs[1] = level > 1.0e-6 ? (float) (20.0 * std::log10 (level)) : floorDb;
        }

    private:
        struct Cache
        {
            float ms = -1.0f;
            double coefficient = 0.0;
        };

        double coefficientFor (float ms, Cache& cache) noexcept
        {
            if (ms != cache.ms)
            {
                cache.ms = ms;
                const auto samples = juce::jmax (1.0e-3, (double) ms * 0.001 * sampleRate);
                cache.coefficient = std::exp (-1.0 / samples);
            }
            return cache.coefficient;
        }

        struct Window
        {
            float ms = -1.0f;
            double alpha = 1.0;
            double get (float newMs, double fs) noexcept
            {
                if (newMs != ms)
                {
                    ms = newMs;
                    alpha = 1.0 - std::exp (-1.0 / ((double) newMs * 0.001 * fs));
                }
                return alpha;
            }
        };

        Mode mode = Mode::Rms;
        double sampleRate = 48000.0, envelope = 0.0, power = 0.0;
        Window rmsWindow;
        float storedAttackMs = 10.0f, storedReleaseMs = 150.0f;
        Cache attackCache, releaseCache;
    };
}
