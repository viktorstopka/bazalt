#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace bazalt::engine::nodes
{
    /** Stable type id: "data.scale" (wiki/NODES.md's `data.*` row, the Data
        Foundations batch). Publishes a `Data(scale)` buffer: the sorted,
        deduplicated set of scale-degree offsets (0..octaveSize-1) a scale
        occupies, already rotated by `root`. This is the pathfinder for the
        whole `Data`-publishing pipeline — no node in the engine produced a
        real `Data` value before this one (`wiki/NODES.Status.md`'s own
        cross-cutting prerequisite note); `Node::getDataPublisher()`/
        `setDataInput()` and `GraphCompiler.cpp`'s new Data-connection
        branch exist because of this node, not the other way around.

        **12 named scales are built-in** — everything the catalog names
        except two deliberately deferred items: "harmonic series" (a real,
        but genuinely ambiguous request — there's no single agreed 12-tone
        approximation, and a poor guess would be worse than an honest gap)
        and "custom" (wants real per-degree content editing, which belongs
        on `NodeContent` — `wiki/NODES.System.md` §3 — once that exists, not
        a fixed parameter bank hacked in ahead of it). The 7 church modes
        (major/Ionian through Locrian), both pentatonics, blues, whole tone,
        and chromatic are all real.

        **`octaveSize`** (structural, default 12) generalizes the named
        12-tone patterns to other divisions: each pattern degree is scaled
        by `octaveSize / 12` before rotation — e.g. at `octaveSize = 24`,
        every semitone becomes two steps. This is a real, defensible
        generalization of a 12-tone pattern table, not just 12-tone with
        extra steps — verified by a dedicated test.

        **A real, documented RT-safety limit, not an oversight**: `root` is
        a genuine, wireable `PortDescriptor` (matching the catalog), but its
        *live*, cable-fed value is never read on the audio thread — only the
        value applied via `setParameter()` (the node's own inline slider,
        same mechanism `adapt.threshold`'s "threshold" port already uses)
        rebuilds and republishes the scale. Rebuilding means constructing a
        new `std::vector<float>`/`DataBuffer` — a heap allocation
        (CLAUDE.md rule 2 forbids this on the audio thread) — so a cable
        wired into `root` compiles and is accepted, but currently has no
        audible effect. A real worker-thread content-rebuild pipeline
        (`wiki/NODES.System.md` §8's own still-open item) is what closes
        this properly; inventing a bespoke lock-free in-place-mutation
        scheme just for this one node was judged out of scope for the
        "Data Foundations" batch specifically.
    */
    class DataScaleNode : public Node
    {
    public:
        static constexpr int numInputs = 1;  // root
        static constexpr int numOutputs = 1; // data (wasted float slot, like every Note-output node's own unused slot)

        enum class ScaleType
        {
            Major, Dorian, Phrygian, Lydian, Mixolydian, Aeolian, Locrian,
            MajorPentatonic, MinorPentatonic, Blues, WholeTone, Chromatic
        };

        void prepare (const NodePrepareInfo&) override { rebuildAndPublish(); }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Scale"; }
        juce::String getCategory() const override { return "Data"; }

        bool supportsPerSample() const noexcept override { return false; }
        void processBlock (const float* const*, float* const*, int) noexcept override {} // no per-sample work at all

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { ValueTypes::midiNotePort ("data.scale.root", "Root", 60.0f) };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "data", .type = SignalType::Data, .label = "Data", .isPrimaryOutput = true,
                                  .dataTags = { DataTag::Scale } },
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "data.scale.scale",
                                       .minValue = 0.0f, .maxValue = 11.0f, .defaultValue = 0.0f,
                                       .displayName = "Scale", .isInteger = true, .kind = ValueKind::Enum,
                                       .enumOptions = { { "major", "Major" }, { "dorian", "Dorian" },
                                                         { "phrygian", "Phrygian" }, { "lydian", "Lydian" },
                                                         { "mixolydian", "Mixolydian" }, { "aeolian", "Aeolian" },
                                                         { "locrian", "Locrian" },
                                                         { "majorPentatonic", "Major Pentatonic" },
                                                         { "minorPentatonic", "Minor Pentatonic" },
                                                         { "blues", "Blues" }, { "wholeTone", "Whole Tone" },
                                                         { "chromatic", "Chromatic" } },
                                       .isStructural = true },
                ParameterDescriptor { .id = "data.scale.octaveSize",
                                       .minValue = 1.0f, .maxValue = 48.0f, .defaultValue = 12.0f,
                                       .displayName = "Octave Size", .isInteger = true, .quantity = Quantity::Count,
                                       .step = 1.0f, .isStructural = true },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "data.scale.root")
                storedRoot = value;
            else if (parameterId == "data.scale.scale")
                scale = (ScaleType) juce::jlimit (0, 11, (int) std::lround (value));
            else if (parameterId == "data.scale.octaveSize")
                octaveSize = juce::jmax (1, (int) std::lround (value));
            else
                return;

            rebuildAndPublish();
        }

        DataPublisher* getDataPublisher() noexcept override { return &dataPublisher; }

    private:
        static const std::vector<int>& patternFor (ScaleType s) noexcept
        {
            // Degrees within one 12-tone octave, root-relative (0 = root).
            static const std::vector<int> major { 0, 2, 4, 5, 7, 9, 11 };
            static const std::vector<int> dorian { 0, 2, 3, 5, 7, 9, 10 };
            static const std::vector<int> phrygian { 0, 1, 3, 5, 7, 8, 10 };
            static const std::vector<int> lydian { 0, 2, 4, 6, 7, 9, 11 };
            static const std::vector<int> mixolydian { 0, 2, 4, 5, 7, 9, 10 };
            static const std::vector<int> aeolian { 0, 2, 3, 5, 7, 8, 10 };
            static const std::vector<int> locrian { 0, 1, 3, 5, 6, 8, 10 };
            static const std::vector<int> majorPentatonic { 0, 2, 4, 7, 9 };
            static const std::vector<int> minorPentatonic { 0, 3, 5, 7, 10 };
            static const std::vector<int> blues { 0, 3, 5, 6, 7, 10 };
            static const std::vector<int> wholeTone { 0, 2, 4, 6, 8, 10 };
            static const std::vector<int> chromatic { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

            switch (s)
            {
                case ScaleType::Dorian:           return dorian;
                case ScaleType::Phrygian:         return phrygian;
                case ScaleType::Lydian:           return lydian;
                case ScaleType::Mixolydian:       return mixolydian;
                case ScaleType::Aeolian:          return aeolian;
                case ScaleType::Locrian:          return locrian;
                case ScaleType::MajorPentatonic:  return majorPentatonic;
                case ScaleType::MinorPentatonic:  return minorPentatonic;
                case ScaleType::Blues:            return blues;
                case ScaleType::WholeTone:        return wholeTone;
                case ScaleType::Chromatic:        return chromatic;
                case ScaleType::Major:
                default:                          return major;
            }
        }

        void rebuildAndPublish()
        {
            const auto& pattern = patternFor (scale);
            const auto rootClass = ((((int) std::lround (storedRoot)) % octaveSize) + octaveSize) % octaveSize;

            std::vector<float> values;
            values.reserve (pattern.size());

            for (const auto degree : pattern)
            {
                const auto scaledDegree = octaveSize == 12 ? degree : (int) std::lround ((double) degree * octaveSize / 12.0);
                const auto withRoot = ((scaledDegree + rootClass) % octaveSize + octaveSize) % octaveSize;
                values.push_back ((float) withRoot);
            }

            std::sort (values.begin(), values.end());
            values.erase (std::unique (values.begin(), values.end()), values.end()); // octaveSize rescaling can collapse two degrees onto one

            dataPublisher.publish (std::make_unique<DataBuffer> (DataTag::Scale, std::move (values), 1));
        }

        DataPublisher dataPublisher;
        float storedRoot = 60.0f;
        ScaleType scale = ScaleType::Major;
        int octaveSize = 12;
    };
}
