#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <array>
#include <cmath>

namespace bazalt::engine::nodes
{
    namespace dynamics
    {
        /** One-pole smoothing coefficient for a time constant, cached. */
        struct Coefficient
        {
            float ms = -1.0f;
            double value = 0.0;

            double get (float newMs, double sampleRate) noexcept
            {
                if (newMs != ms)
                {
                    ms = newMs;
                    value = std::exp (-1.0 / juce::jmax (1.0e-3, (double) newMs * 0.001 * sampleRate));
                }
                return value;
            }
        };

        inline PortDescriptor controlPort (const char* id, const char* label, const char* unit, float minValue, float maxValue,
                                           float defaultValue, Quantity quantity = Quantity::Dimensionless)
        {
            return PortDescriptor { .id = id, .type = SignalType::Signal, .label = label, .unit = unit,
                                    .minValue = minValue, .maxValue = maxValue, .defaultValue = defaultValue,
                                    .hasFallbackWhenUnconnected = true, .quantity = quantity };
        }

        /** The detector input: the sidechain when one is wired, else the
            signal itself; both channels linked (the louder one), so the
            stereo image never shifts. Stereo-in, the port reads NaN when
            unconnected (hasFallbackWhenUnconnected). */
        inline double linkedPeak (const float* inputs, int sidechainIndex) noexcept
        {
            const auto useSidechain = ! std::isnan (inputs[sidechainIndex]);
            const auto* source = useSidechain ? inputs + sidechainIndex : inputs;
            return std::max (std::abs ((double) source[0]), std::abs ((double) source[1]));
        }
    }

    /** Stable type id: "dyn.compress" (wiki/plans/SoundPalette.md, Batch 3).
        A stereo-linked feed-forward compressor with a soft knee (Giannoulis,
        Massberg & Reiss 2012): above `threshold`, every `ratio` dB in gives
        1 dB out; `knee` rounds the corner; attack/release smooth the gain in
        dB. A `sidechain` input, when wired, decides the gain instead of the
        signal itself — ducking.

        The digital-native part: the gain isn't locked inside. `gain` (the
        linear multiplier being applied, ≤ 1) and `reduction` (in dB) are
        outputs, so the same envelope can duck anything else in the patch —
        a filter, a send, a whole other voice — by wiring, not by a special
        mode. */
    class DynCompressNode : public Node
    {
    public:
        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            reset();
        }
        void reset() override { gainDb = detector = 0.0; }

        int getNumInputPorts() const noexcept override { return 9; }
        int getNumOutputPorts() const noexcept override { return 3; }
        int getNumInputChannels() const noexcept override { return 11; }
        int getNumOutputChannels() const noexcept override { return 4; }
        juce::String getTitle() const override { return "Compressor"; }
        juce::String getCategory() const override { return "Dynamics"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            using dynamics::controlPort;
            return {
                PortDescriptor { .id = "in", .type = SignalType::Signal, .quantity = Quantity::Audio, .channels = Channels::Stereo },
                PortDescriptor { .id = "sidechain", .type = SignalType::Signal, .label = "Sidechain",
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Audio, .channels = Channels::Stereo },
                controlPort ("dyn.compress.threshold", "Threshold", "dB", -60.0f, 0.0f, -18.0f, Quantity::Gain),
                controlPort ("dyn.compress.ratio", "Ratio", ":1", 1.0f, 20.0f, 4.0f, Quantity::Ratio),
                controlPort ("dyn.compress.knee", "Knee", "dB", 0.0f, 24.0f, 6.0f, Quantity::Gain),
                ValueTypes::timeMsPort ("dyn.compress.attack", "Attack", 10.0f, 200.0f),
                ValueTypes::timeMsPort ("dyn.compress.release", "Release", 120.0f, 2000.0f),
                controlPort ("dyn.compress.makeup", "Makeup", "dB", 0.0f, 24.0f, 0.0f, Quantity::Gain),
                controlPort ("dyn.compress.mix", "Mix", "", 0.0f, 1.0f, 1.0f, Quantity::Unipolar),
            };
        }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "out", .type = SignalType::Signal, .label = "Out", .isPrimaryOutput = true, .quantity = Quantity::Audio, .channels = Channels::Stereo },
                PortDescriptor { .id = "gain", .type = SignalType::Signal, .label = "Gain", .minValue = 0.0f, .maxValue = 1.0f,
                                  .quantity = Quantity::Unipolar },
                PortDescriptor { .id = "reduction", .type = SignalType::Signal, .label = "Reduction", .unit = "dB",
                                  .minValue = 0.0f, .maxValue = 60.0f, .quantity = Quantity::Gain },
            };
        }
        void setParameter (const juce::String& parameterId, float value) override
        {
            for (int i = 0; i < 7; ++i)
                if (parameterId == ids()[(size_t) i])
                    stored[(size_t) i] = value;
        }

        /** The static curve: output level for an input level, both in dB. */
        static double curve (double x, double threshold, double ratio, double knee) noexcept
        {
            const auto over = x - threshold;
            if (2.0 * over < -knee)
                return x;
            if (knee > 0.0 && 2.0 * std::abs (over) <= knee)
            {
                const auto t = over + knee * 0.5;
                return x + (1.0 / ratio - 1.0) * t * t / (2.0 * knee);
            }
            return threshold + over / ratio;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            float v[7];
            for (int i = 0; i < 7; ++i)
                v[i] = std::isnan (inputs[4 + i]) ? stored[(size_t) i] : inputs[4 + i];
            const auto threshold = (double) juce::jlimit (-80.0f, 0.0f, v[0]);
            const auto ratio = (double) juce::jlimit (1.0f, 100.0f, v[1]);
            const auto knee = (double) juce::jlimit (0.0f, 48.0f, v[2]);

            // Peak detector (instant attack, the release time) ahead of the
            // gain computer, so a steady tone is judged by its peak, not by
            // every point of its waveform.
            const auto level = dynamics::linkedPeak (inputs, 2);
            detector = level > detector ? level : level + detectorRelease.get (v[4], sampleRate) * (detector - level);
            const auto levelDb = detector > 1.0e-6 ? 20.0 * std::log10 (detector) : -120.0;
            const auto targetDb = curve (levelDb, threshold, ratio, knee) - levelDb; // <= 0

            const auto coefficient = targetDb < gainDb ? attack.get (v[3], sampleRate) : release.get (v[4], sampleRate);
            gainDb = targetDb + coefficient * (gainDb - targetDb);

            const auto gain = std::pow (10.0, gainDb / 20.0);
            const auto makeup = std::pow (10.0, juce::jlimit (0.0f, 48.0f, v[5]) / 20.0);
            const auto mix = (double) juce::jlimit (0.0f, 1.0f, v[6]);
            const auto applied = (1.0 - mix) + mix * gain * makeup;

            outputs[0] = (float) (inputs[0] * applied);
            outputs[1] = (float) (inputs[1] * applied);
            outputs[2] = (float) gain;
            outputs[3] = (float) -gainDb;
        }

    private:
        static const std::array<juce::String, 7>& ids()
        {
            static const std::array<juce::String, 7> list { "dyn.compress.threshold", "dyn.compress.ratio", "dyn.compress.knee",
                                                            "dyn.compress.attack", "dyn.compress.release", "dyn.compress.makeup",
                                                            "dyn.compress.mix" };
            return list;
        }

        double sampleRate = 48000.0, gainDb = 0.0, detector = 0.0;
        std::array<float, 7> stored { -18.0f, 4.0f, 6.0f, 10.0f, 120.0f, 0.0f, 1.0f };
        dynamics::Coefficient attack, release, detectorRelease;
    };

    /** Stable type id: "dyn.gate" (wiki/plans/SoundPalette.md, Batch 3).
        A stereo-linked gate / expander: opens when the (side)chain level rises
        above `threshold`, closes `hold` ms after it falls 3 dB below it
        (hysteresis, so it doesn't chatter at the edge). Closed means `range`
        dB down — 80 dB is a hard gate, 10 dB a gentle expander. Like the
        compressor, the decision is an output: `open` (Boolean) and `gain`
        can gate or trigger anything else in the patch. */
    class DynGateNode : public Node
    {
    public:
        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            reset();
        }
        void reset() override
        {
            gain = 0.0;
            isOpen = false;
            holdRemaining = 0.0;
            detector = 0.0;
        }

        int getNumInputPorts() const noexcept override { return 7; }
        int getNumOutputPorts() const noexcept override { return 3; }
        int getNumInputChannels() const noexcept override { return 9; }
        int getNumOutputChannels() const noexcept override { return 4; }
        juce::String getTitle() const override { return "Gate"; }
        juce::String getCategory() const override { return "Dynamics"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            using dynamics::controlPort;
            return {
                PortDescriptor { .id = "in", .type = SignalType::Signal, .quantity = Quantity::Audio, .channels = Channels::Stereo },
                PortDescriptor { .id = "sidechain", .type = SignalType::Signal, .label = "Sidechain",
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Audio, .channels = Channels::Stereo },
                controlPort ("dyn.gate.threshold", "Threshold", "dB", -80.0f, 0.0f, -40.0f, Quantity::Gain),
                controlPort ("dyn.gate.range", "Range", "dB", 0.0f, 80.0f, 60.0f, Quantity::Gain),
                ValueTypes::timeMsPort ("dyn.gate.attack", "Attack", 1.0f, 50.0f),
                ValueTypes::timeMsPort ("dyn.gate.hold", "Hold", 20.0f, 500.0f),
                ValueTypes::timeMsPort ("dyn.gate.release", "Release", 100.0f, 2000.0f),
            };
        }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "out", .type = SignalType::Signal, .label = "Out", .isPrimaryOutput = true, .quantity = Quantity::Audio, .channels = Channels::Stereo },
                PortDescriptor { .id = "gain", .type = SignalType::Signal, .label = "Gain", .minValue = 0.0f, .maxValue = 1.0f,
                                  .quantity = Quantity::Unipolar },
                PortDescriptor { .id = "open", .type = SignalType::Signal, .label = "Open", .kind = ValueKind::Bool, .quantity = Quantity::Boolean },
            };
        }
        void setParameter (const juce::String& parameterId, float value) override
        {
            static const juce::String ids[] = { "dyn.gate.threshold", "dyn.gate.range", "dyn.gate.attack", "dyn.gate.hold", "dyn.gate.release" };
            for (int i = 0; i < 5; ++i)
                if (parameterId == ids[i])
                    stored[(size_t) i] = value;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            float v[5];
            for (int i = 0; i < 5; ++i)
                v[i] = std::isnan (inputs[4 + i]) ? stored[(size_t) i] : inputs[4 + i];

            // A fast peak detector (instant attack, 10 ms release) so the
            // gate sees the level, not single zero crossings.
            const auto level = dynamics::linkedPeak (inputs, 2);
            detector = level > detector ? level : level + detectorRelease.get (10.0f, sampleRate) * (detector - level);
            const auto levelDb = detector > 1.0e-6 ? 20.0 * std::log10 (detector) : -120.0;

            if (levelDb > v[0])
            {
                isOpen = true;
                holdRemaining = (double) v[3] * 0.001 * sampleRate;
            }
            else if (levelDb < v[0] - 3.0f)
            {
                if (holdRemaining > 0.0)
                    holdRemaining -= 1.0;
                else
                    isOpen = false;
            }

            const auto closed = std::pow (10.0, -(double) juce::jlimit (0.0f, 120.0f, v[1]) / 20.0);
            const auto target = isOpen ? 1.0 : closed;
            const auto coefficient = target > gain ? attack.get (v[2], sampleRate) : release.get (v[4], sampleRate);
            gain = target + coefficient * (gain - target);

            outputs[0] = (float) (inputs[0] * gain);
            outputs[1] = (float) (inputs[1] * gain);
            outputs[2] = (float) gain;
            outputs[3] = isOpen ? 1.0f : 0.0f;
        }

    private:
        double sampleRate = 48000.0, gain = 0.0, holdRemaining = 0.0, detector = 0.0;
        bool isOpen = false;
        std::array<float, 5> stored { -40.0f, 60.0f, 1.0f, 20.0f, 100.0f };
        dynamics::Coefficient attack, release, detectorRelease;
    };
}
