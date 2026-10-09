#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/nodes/InstanceSeeding.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace bazalt::engine::nodes
{
    /** Stable type id: "data.material" (wiki/NODES.md's `data.*` row, the PM
        Core batch). The physical-modelling counterpart of `data.scale` —
        describes a resonating object's mode set (`resonator.modal`'s own
        required `modes` input) from a handful of physical parameters rather
        than a hand-specified frequency list. Same RT-safety shape as
        `data.scale`: every port here is a real, wireable `Control` input,
        but only a `setParameter()`-driven change actually rebuilds and
        republishes (rebuilding means a heap allocation — forbidden on the
        audio thread, CLAUDE.md rule 2) — a live cable has no audible effect
        today, exactly `data.scale`'s own documented limit.

        Publishes `Data(modal-set)`, `stride = 3`: per mode, `(ratio,
        amplitudeWeight, decayWeight)` — `Data.h`'s own doc comment names
        this exact triple as the motivating example for why `DataBuffer` is
        generic rather than a fixed per-tag C++ type. `ratio` is relative to
        a fundamental of `1.0` — `resonator.modal`'s own `pitch` input is
        what turns this into real Hz; this node has no absolute-pitch
        concept at all, by design (two different nodes answering two
        different questions, same separation `data.scale`'s scale-degree
        offsets vs. `note.quantize`'s absolute pitch already establishes).

        **The catalog gives this node six input names and nothing else**
        ("physical parameters" is all it says) — the concrete mapping from
        each knob to the generated mode set is this node's own design,
        documented here the same way `data.lookup`'s mode contract and
        `data.scale`'s octaveSize generalization are: a real, considered
        choice, not the only possible one.

        **Per-geometry base ratio set** (mode `n = 1..modeCount`, before any
        of the knobs below are applied):
        - `string`: `n` — the plain harmonic series.
        - `tube`: `2n - 1` — odd harmonics only (closed-open, clarinet-like;
          the open-open/closed-closed case is `resonator.tube`'s own future
          `endCondition`, not this node's job).
        - `bar`: `((n + 0.5) / 1.5)^2` — the free-free Euler-Bernoulli beam's
          large-`n` asymptotic approximation (`βL ≈ (n + 1/2)π`, frequency ∝
          `(βL)^2`); checked against the standard reference values for a
          free-free bar (Fletcher & Rossing) — within ~1% even at `n = 2..4`,
          not just asymptotically.
        - `membrane`: the first 12 circular-membrane modes (Bessel zeros of
          `J0`/`J1`/`J2`/`J3`, sorted and normalized to the fundamental),
          hardcoded — continued past mode 12 by linear extrapolation of the
          table's own tail spacing (real 2D mode density keeps growing
          roughly linearly in index for a fixed order, so this is a
          reasonable continuation, not a new physical derivation).
        - `plate`: the `membrane` table's own ratios, **squared** — a real,
          documented simplification: reuses the membrane's relative nodal
          pattern (both are 2D circular domains, broadly similar mode
          shapes to first order) but applies a plate's bending-stiffness
          dispersion (`frequency ∝ k²`) instead of a membrane's
          tension-only one (`frequency ∝ k`); `membraneRatio = k/k₁` so
          `plateRatio = (k/k₁)² = membraneRatio²` exactly. Not a literal
          Chladni/finite-element solve — a real, honest approximation,
          audibly denser/stiffer-feeling at the top than the membrane,
          which is the actual perceptual target.
        - `irregularSolid`: no closed form at all — each mode is the
          previous one plus a deterministic pseudo-random gap (seeded from
          `(seed, n)` via `combineInstanceSeed`, shared with every other
          instance-seeded node in this codebase), giving a genuinely
          non-periodic, bell/rock-like spectrum by construction.

        **Then, uniformly, regardless of geometry:**
        1. **Stiffness-scaled inharmonicity stretch** — the classic stiff-
           string formula, `ratio *= sqrt(1 + B·n²)`, `B = inharmonicity ·
           (0.0001 + stiffness · 0.001)`. `inharmonicity` is the on/off
           master (zero means zero stretch regardless of `stiffness`);
           `stiffness` sets how strong the effect gets once `inharmonicity`
           is non-zero — a physically sensible pair (a stiffer string has
           MORE inharmonicity for the same dialed-in amount), not two
           independent additive knobs.
        2. **Irregularity jitter** — `ratio *= 1 + irregularity · jitter_n`,
           `jitter_n` a deterministic ±15%-max value seeded from
           `(seed, n)`. The whole set is re-sorted ascending afterward
           (`irregularity` can reorder nearby modes; a "mode set" is
           expected to read low-to-high).
        3. **Density/damping-scaled decay weight** — `decayWeight_n =
           exp(-density · damping · (n-1)^1.3 · 0.15)`, `1.0` for the
           fundamental always, dropping toward `0` for higher modes as
           either knob increases (their PRODUCT, not a sum — both must be
           non-zero for any differential decay at all; a real design
           choice, not an oversight). `resonator.modal`'s own `decay`
           sets the nominal rate this WEIGHTS, not an absolute time.
        4. **Preset-driven amplitude weight** — `ampWeight_n = n ^
           -brightnessExponent`, `brightnessExponent` fixed per `preset`
           (wood 1.4, stone 1.1, bone 1.0/custom 1.0, ceramic 0.8, ice 0.7,
           glass 0.6, metal 0.5 — lower exponent = brighter/slower rolloff).
           This is this node's own documented, non-measured characterization
           of each material name, the same spirit as `space.pan`'s own
           "-4.5dB law" design call: plausible, not a laboratory measurement.

        **`size` is a declared port, matching the catalog, but is NOT YET
        consumed** by the generation algorithm above — a real, deliberate
        MVP gap, same treatment `data.table`'s own `loop` flag already gets
        ("carried as real metadata... not consumed by anything yet") rather
        than inventing an arbitrary effect just to make every knob do
        something. A real role for it (mode density / overall registral
        compression for the non-deterministic geometries) is future work,
        not silently assumed to exist.
    */
    class DataMaterialNode : public Node
    {
    public:
        static constexpr int maxModeCount = 64;
        static constexpr int numInputs = 6;  // stiffness, density, damping, size, inharmonicity, irregularity
        static constexpr int numOutputs = 1; // data (wasted float slot, like every other Data-output node)

        enum class Geometry { String, Bar, Tube, Membrane, Plate, IrregularSolid };
        enum class Preset { Wood, Glass, Metal, Stone, Ceramic, Bone, Ice, Custom };

        void prepare (const NodePrepareInfo&) override { rebuildAndPublish(); }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Material"; }
        juce::String getCategory() const override { return "Data"; }

        bool supportsPerSample() const noexcept override { return false; }
        void processBlock (const float* const*, float* const*, int) noexcept override {}

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                unipolarPort ("data.material.stiffness", "Stiffness", 0.3f),
                unipolarPort ("data.material.density", "Density", 0.3f),
                unipolarPort ("data.material.damping", "Damping", 0.3f),
                unipolarPort ("data.material.size", "Size", 0.5f),
                unipolarPort ("data.material.inharmonicity", "Inharmonicity", 0.2f),
                unipolarPort ("data.material.irregularity", "Irregularity", 0.0f),
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "data", .type = SignalType::Data, .label = "Data", .isPrimaryOutput = true,
                                  .dataTags = { DataTag::ModalSet } },
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "data.material.geometry",
                                       .minValue = 0.0f, .maxValue = 5.0f, .defaultValue = 0.0f,
                                       .displayName = "Geometry", .isInteger = true, .kind = ValueKind::Enum,
                                       .enumOptions = { { "string", "String" }, { "bar", "Bar" }, { "tube", "Tube" },
                                                         { "membrane", "Membrane" }, { "plate", "Plate" },
                                                         { "irregularSolid", "Irregular Solid" } },
                                       .isStructural = true },
                ParameterDescriptor { .id = "data.material.modeCount",
                                       .minValue = 1.0f, .maxValue = (float) maxModeCount, .defaultValue = 32.0f,
                                       .displayName = "Mode Count", .isInteger = true, .quantity = Quantity::Count,
                                       .step = 1.0f, .isStructural = true },
                ParameterDescriptor { .id = "data.material.preset",
                                       .minValue = 0.0f, .maxValue = 7.0f, .defaultValue = 0.0f,
                                       .displayName = "Preset", .isInteger = true, .kind = ValueKind::Enum,
                                       .enumOptions = { { "wood", "Wood" }, { "glass", "Glass" }, { "metal", "Metal" },
                                                         { "stone", "Stone" }, { "ceramic", "Ceramic" },
                                                         { "bone", "Bone" }, { "ice", "Ice" }, { "custom", "Custom" } },
                                       .isStructural = true },
                ParameterDescriptor { .id = "data.material.seed",
                                       .minValue = 0.0f, .maxValue = 999999.0f, .defaultValue = 1.0f,
                                       .displayName = "Seed", .isInteger = true, .isStructural = true },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "data.material.stiffness")
                stiffness = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "data.material.density")
                density = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "data.material.damping")
                damping = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "data.material.size")
                size = juce::jlimit (0.0f, 1.0f, value); // stored; not yet consumed, see class comment
            else if (parameterId == "data.material.inharmonicity")
                inharmonicity = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "data.material.irregularity")
                irregularity = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "data.material.geometry")
                geometry = (Geometry) juce::jlimit (0, 5, (int) std::lround (value));
            else if (parameterId == "data.material.modeCount")
                modeCount = juce::jlimit (1, maxModeCount, (int) std::lround (value));
            else if (parameterId == "data.material.preset")
                preset = (Preset) juce::jlimit (0, 7, (int) std::lround (value));
            else if (parameterId == "data.material.seed")
                seed = (int) std::lround (value);
            else
                return;

            rebuildAndPublish();
        }

        DataPublisher* getDataPublisher() noexcept override { return &dataPublisher; }

    private:
        static PortDescriptor unipolarPort (juce::String id, juce::String label, float defaultValue)
        {
            return PortDescriptor { .id = std::move (id), .type = SignalType::Signal, .label = std::move (label),
                                     .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = defaultValue,
                                     .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar,
                                     .polarity = Polarity::Unipolar };
        }

        // First 12 known zeros of J0/J1/J2/J3 (circular-membrane modes),
        // sorted ascending and normalized to the fundamental (J0's first
        // zero, 2.405). Standard reference values (e.g. Fletcher & Rossing,
        // "The Physics of Musical Instruments").
        static const std::vector<float>& membraneRatioTable() noexcept
        {
            static const std::vector<float> table {
                1.000f, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f,
                3.156f, 3.500f, 3.600f, 3.652f, 3.988f, 4.059f
            };
            return table;
        }

        float baseRatio (int n) const noexcept // n is 1-based
        {
            switch (geometry)
            {
                case Geometry::Tube:
                    return (float) (2 * n - 1);

                case Geometry::Bar:
                {
                    const auto x = ((float) n + 0.5f) / 1.5f;
                    return x * x;
                }

                case Geometry::Membrane:
                case Geometry::Plate:
                {
                    const auto& table = membraneRatioTable();
                    float ratio;
                    if (n <= (int) table.size())
                        ratio = table[(size_t) (n - 1)];
                    else
                    {
                        // Continue past the table using its own tail
                        // spacing (real 2D mode density grows roughly
                        // linearly in index for a fixed order).
                        const auto lastSpacing = table.back() - table[table.size() - 2];
                        const auto extra = n - (int) table.size();
                        ratio = table.back() + lastSpacing * (float) extra;
                    }
                    return geometry == Geometry::Plate ? ratio * ratio : ratio;
                }

                case Geometry::IrregularSolid:
                {
                    // Deterministic cumulative pseudo-random gaps, seeded
                    // per-mode — never a closed form, by design (see class
                    // comment).
                    float ratio = 1.0f;
                    for (int i = 2; i <= n; ++i)
                    {
                        const auto raw = combineInstanceSeed (seed, i * 7919); // a fixed prime offset keeps this stream distinct from the jitter stream below
                        const auto unit = (float) ((uint64_t) raw % 1000000) / 1000000.0f; // [0,1)
                        const auto gap = 0.3f + unit * 2.2f; // [0.3, 2.5)
                        ratio += gap;
                    }
                    return ratio;
                }

                case Geometry::String:
                default:
                    return (float) n;
            }
        }

        void rebuildAndPublish()
        {
            const auto inharmonicityB = inharmonicity * (0.0001f + stiffness * 0.001f);
            const auto brightnessExponent = brightnessExponentFor (preset);

            std::vector<float> ratios (((size_t) modeCount));
            for (int i = 0; i < modeCount; ++i)
            {
                const auto n = i + 1;
                auto ratio = baseRatio (n);

                // 1. Stiffness-scaled inharmonicity stretch.
                ratio *= std::sqrt (1.0f + inharmonicityB * (float) n * (float) n);

                // 2. Irregularity jitter, deterministic per (seed, n).
                if (irregularity > 0.0f)
                {
                    const auto raw = combineInstanceSeed (seed, n);
                    const auto unit = (float) ((uint64_t) raw % 1000000) / 1000000.0f; // [0,1)
                    const auto jitter = (unit * 2.0f - 1.0f) * 0.15f; // +/-15% max
                    ratio *= 1.0f + irregularity * jitter;
                }

                ratios[(size_t) i] = ratio;
            }

            std::sort (ratios.begin(), ratios.end());

            std::vector<float> values;
            values.reserve ((size_t) modeCount * 3);

            for (int i = 0; i < modeCount; ++i)
            {
                const auto n = i + 1;
                const auto decayWeight = std::exp (-density * damping * std::pow ((float) (n - 1), 1.3f) * 0.15f);
                const auto ampWeight = std::pow ((float) n, -brightnessExponent);

                values.push_back (ratios[(size_t) i]);
                values.push_back (ampWeight);
                values.push_back (decayWeight);
            }

            dataPublisher.publish (std::make_unique<DataBuffer> (DataTag::ModalSet, std::move (values), 3));
        }

        static float brightnessExponentFor (Preset p) noexcept
        {
            switch (p)
            {
                case Preset::Wood:    return 1.4f;
                case Preset::Glass:   return 0.6f;
                case Preset::Metal:   return 0.5f;
                case Preset::Stone:   return 1.1f;
                case Preset::Ceramic: return 0.8f;
                case Preset::Bone:    return 1.0f;
                case Preset::Ice:     return 0.7f;
                case Preset::Custom:
                default:              return 1.0f;
            }
        }

        DataPublisher dataPublisher;
        float stiffness = 0.3f, density = 0.3f, damping = 0.3f, size = 0.5f, inharmonicity = 0.2f, irregularity = 0.0f;
        Geometry geometry = Geometry::String;
        int modeCount = 32;
        Preset preset = Preset::Wood;
        int seed = 1;
    };
}
