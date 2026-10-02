#pragma once

#include "bazalt/engine/graph/Node.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace bazalt::engine::nodes
{
    /** Stable type id: "resonator.plate" (wiki/NODES.md's `resonator.*` row,
        the PM Core batch, closing it out — the hardest resonator in the
        catalog). "A 2D waveguide mesh — the resonating body behind drum
        heads, plates, and gongs" (the catalog's own framing); this node is
        self-contained (no `modes` input, unlike `resonator.modal`) and
        generates its own mode set internally from `size`/`tension`.

        **A real, documented simplification, not a literal 2D mesh solve**:
        a true finite-difference 2D waveguide mesh (what the catalog's own
        `quality` enum — low/medium/high — and "inner loop scales with mesh
        size" note seem to envision) is a substantial project of its own.
        This node instead reuses `resonator.modal`'s own two-pole-resonator-
        bank technique, driven by a fixed, internally-generated mode-ratio
        set — the SAME membrane-Bessel-zero table `data.material`'s own
        `plate` geometry already uses, squared the same way (a plate's `k²`
        bending dispersion over a membrane's `k¹` tension-only one) — rather
        than depending on a separately-wired `data.material` node. Audibly
        denser/stiffer-feeling at the top than a plain membrane, which is
        the actual perceptual target; not a derived finite-element solution.
        `quality` sets how many of these internally-generated modes are
        actually used (`low` 8, `medium` 16, `high` 32) rather than mesh
        resolution in any literal spatial sense.

        **No `pitch` input, unlike `resonator.modal`** — the catalog gives
        this node `size`/`tension` instead, so this node derives its OWN
        absolute fundamental: `fundamentalHz = map(size, 0→1, 2000→80 Hz) ·
        sqrt(map(tension, 0→1, 0.3→2.5))` — smaller/tenser plates ring
        higher, bigger/looser ones ring lower, a real, considered, but
        unmeasured mapping (this node's own design call, same spirit as
        `data.material`'s preset brightness exponents).

        **`damping`** reuses the SAME 60dB-decay-weighting idea
        `resonator.modal`'s own `brightness` uses, just under this node's
        OWN catalog-given name: `0` = darkest (extra per-mode damping on the
        overtones), `1` = brightest (none) — kept at the SAME semantic
        direction `resonator.comb`/`resonator.string`/`filter.onepole`'s own
        "Damping" ports already establish project-wide, not a second,
        contradictory convention under a name that happens to be reused.

        **`positionX`/`positionY`**: a 2D generalization of
        `resonator.modal`'s own `|sin((k+1)πposition)|` mode-shape weighting
        — `|sin((k+1)πpositionX) · cos((k+1)πpositionY·0.7)|` — a documented
        approximation (a real 2D plate's mode shapes are genuinely 2D
        products of transverse functions; this isn't a literal derivation of
        THIS plate's actual eigenmodes, just a plausible, tunable stand-in
        with two independent, audibly different axes).

        **Stereo spread is unconditional, not a separate knob** (the catalog
        gives this node no `spread` control at all, unlike `resonator.modal`):
        every mode is deterministically panned via the SAME golden-angle
        constant `resonator.modal` uses, always on — a real plate's own
        complex mode mixture is rarely perceived as perfectly centered
        either, and this keeps the node's own knob count matching the
        catalog exactly rather than inventing an extra one.
    */
    class ResonatorPlateNode : public Node
    {
    public:
        static constexpr float goldenAngleRad = 2.399963f;
        static constexpr int numInputs = 7;  // excite, size, tension, decay, damping, positionX, positionY
        static constexpr int numOutputs = 1; // out (Stereo — 2 flat channels)

        enum class Quality { Low, Medium, High };

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            // Always allocate to the CEILING (High, 32 modes), never the
            // current `quality` member — a real, confirmed-live crash this
            // session's own direct feedback caught: GraphCompiler.cpp
            // calls prepare() BEFORE applying a freshly-constructed node's
            // stored parameters (it prepares first, then loops
            // `setParameter()` over every saved parameter) — so `quality`
            // is still at its just-constructed default (`Medium`, 16) the
            // moment this runs, regardless of what the graph's own saved
            // "resonator.plate.quality" value will shortly set it to.
            // Sizing from the live member here left state1/state2 at 16
            // elements even when `setParameter()` moments later raised
            // `quality` to `High` (32) — processSample()'s loop then
            // indexed state1[16..31], a real out-of-bounds vector access.
            // Always sizing to the true ceiling here, and letting
            // `setParameter()`/processSample() only ever use fewer of
            // those already-allocated slots, is the same safe pattern
            // `filter.ladder`'s own fixed-size internal state already uses
            // for its own structural `poles` parameter.
            state1.assign ((size_t) maxModesFor (Quality::High), 0.0f);
            state2.assign ((size_t) maxModesFor (Quality::High), 0.0f);
        }

        void reset() override
        {
            std::fill (state1.begin(), state1.end(), 0.0f);
            std::fill (state2.begin(), state2.end(), 0.0f);
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }
        int getNumOutputChannels() const noexcept override { return 2; }

        juce::String getTitle() const override { return "Plate"; }
        juce::String getCategory() const override { return "Resonators"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                { "excite", SignalType::Audio },
                unipolarPort ("resonator.plate.size", "Size", 0.5f),
                unipolarPort ("resonator.plate.tension", "Tension", 0.5f),
                PortDescriptor { .id = "resonator.plate.decay", .type = SignalType::Control, .label = "Decay",
                                  .unit = "s", .minValue = 0.05f, .maxValue = 30.0f, .defaultValue = 2.0f,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Time,
                                  .curve = Curve::Logarithmic },
                unipolarPort ("resonator.plate.damping", "Damping", 1.0f),
                unipolarPort ("resonator.plate.positionX", "Position X", 0.4f),
                unipolarPort ("resonator.plate.positionY", "Position Y", 0.6f),
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "out", .type = SignalType::Audio, .label = "Out", .isPrimaryOutput = true, .channels = Channels::Stereo },
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "resonator.plate.quality",
                                       .minValue = 0.0f, .maxValue = 2.0f, .defaultValue = 1.0f, // medium
                                       .displayName = "Quality", .isInteger = true, .kind = ValueKind::Enum,
                                       .enumOptions = { { "low", "Low" }, { "medium", "Medium" }, { "high", "High" } },
                                       .isStructural = true },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "resonator.plate.size")
                storedSize = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "resonator.plate.tension")
                storedTension = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "resonator.plate.decay")
                storedDecay = value;
            else if (parameterId == "resonator.plate.damping")
                storedDamping = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "resonator.plate.positionX")
                storedPositionX = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "resonator.plate.positionY")
                storedPositionY = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "resonator.plate.quality")
                quality = (Quality) juce::jlimit (0, 2, (int) std::lround (value));
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto size = std::isnan (inputs[1]) ? storedSize : juce::jlimit (0.0f, 1.0f, inputs[1]);
            const auto tension = std::isnan (inputs[2]) ? storedTension : juce::jlimit (0.0f, 1.0f, inputs[2]);
            const auto decaySeconds = juce::jmax (0.001f, std::isnan (inputs[3]) ? storedDecay : inputs[3]);
            const auto damping = std::isnan (inputs[4]) ? storedDamping : juce::jlimit (0.0f, 1.0f, inputs[4]);
            const auto positionX = std::isnan (inputs[5]) ? storedPositionX : juce::jlimit (0.0f, 1.0f, inputs[5]);
            const auto positionY = std::isnan (inputs[6]) ? storedPositionY : juce::jlimit (0.0f, 1.0f, inputs[6]);

            const auto fundamentalHz = juce::jmap (size, 0.0f, 1.0f, 2000.0f, 80.0f)
                                        * std::sqrt (juce::jmap (tension, 0.0f, 1.0f, 0.3f, 2.5f));
            const auto activeModeCount = maxModesFor (quality);
            const auto excite = inputs[0];
            const auto invSqrtActive = 1.0f / std::sqrt ((float) activeModeCount);
            const auto maxFrequency = (float) (sampleRate * 0.45);

            float left = 0.0f, right = 0.0f;

            for (int k = 0; k < activeModeCount; ++k)
            {
                const auto n = k + 1;
                const auto ratio = plateModeRatio (n);
                const auto frequency = juce::jlimit (1.0f, maxFrequency, fundamentalHz * ratio);
                const auto w = juce::MathConstants<float>::twoPi * frequency / (float) sampleRate;
                const auto cosw = std::cos (w);

                const auto extraDamping = std::exp (-(1.0f - damping) * std::pow ((float) k, 1.2f) * 0.12f);
                const auto effectiveT60 = juce::jmax (0.001f, decaySeconds * extraDamping);
                const auto r = juce::jlimit (0.0f, 0.999999f, std::exp (std::log (0.001f) / (effectiveT60 * (float) sampleRate)));

                const auto positionWeight = std::fabs (std::sin ((float) n * juce::MathConstants<float>::pi * positionX)
                                                        * std::cos ((float) n * juce::MathConstants<float>::pi * positionY * 0.7f));
                const auto ampWeight = std::pow ((float) n, -0.8f);
                const auto gain = ampWeight * positionWeight * invSqrtActive;

                const auto y = 2.0f * r * cosw * state1[(size_t) k] - r * r * state2[(size_t) k] + excite * gain;
                state2[(size_t) k] = state1[(size_t) k];
                state1[(size_t) k] = y;

                const auto pan = std::sin ((float) n * goldenAngleRad);
                const auto leftGain = (1.0f - pan) * 0.5f;
                const auto rightGain = (1.0f + pan) * 0.5f;

                left += y * leftGain;
                right += y * rightGain;
            }

            outputs[0] = left;
            outputs[1] = right;
        }

    private:
        static PortDescriptor unipolarPort (juce::String id, juce::String label, float defaultValue)
        {
            return PortDescriptor { .id = std::move (id), .type = SignalType::Control, .label = std::move (label),
                                     .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = defaultValue,
                                     .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar,
                                     .polarity = Polarity::Unipolar };
        }

        static int maxModesFor (Quality q) noexcept
        {
            switch (q)
            {
                case Quality::Low:    return 8;
                case Quality::High:   return 32;
                case Quality::Medium:
                default:              return 16;
            }
        }

        // The same hardcoded circular-membrane Bessel-zero ratio table
        // data.material's own `membrane`/`plate` geometries use, squared for
        // the same documented bending-vs-tension dispersion reason — kept as
        // a small, separately-hardcoded physics-constant table here rather
        // than a shared abstraction (stable reference values, not logic that
        // needs to stay synchronized by a shared code path).
        static float plateModeRatio (int n) noexcept // n is 1-based
        {
            static const std::vector<float> membraneTable {
                1.000f, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f,
                3.156f, 3.500f, 3.600f, 3.652f, 3.988f, 4.059f
            };

            float ratio;
            if (n <= (int) membraneTable.size())
                ratio = membraneTable[(size_t) (n - 1)];
            else
            {
                const auto lastSpacing = membraneTable.back() - membraneTable[membraneTable.size() - 2];
                const auto extra = n - (int) membraneTable.size();
                ratio = membraneTable.back() + lastSpacing * (float) extra;
            }

            return ratio * ratio;
        }

        double sampleRate = 44100.0;
        std::vector<float> state1, state2;
        Quality quality = Quality::Medium;

        float storedSize = 0.5f, storedTension = 0.5f, storedDecay = 2.0f, storedDamping = 1.0f;
        float storedPositionX = 0.4f, storedPositionY = 0.6f;
    };
}
