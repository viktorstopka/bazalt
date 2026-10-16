#pragma once

#include "bazalt/engine/graph/InstanceGroups.h"
#include "bazalt/engine/nodes/InheritingPortsNode.h"
#include <array>
#include <cmath>
#include <juce_core/juce_core.h>

namespace bazalt::engine::nodes
{
    /** Stable type id: "life.split" — wiki/plans/LifeCycle.md 2b.3. Sorts a
        Population's members into up to four branches; each member is in exactly
        one. Whatever hangs off `branch.k` runs only for the members in branch k
        (the plan skips those steps for everyone else — no CPU for them), and a
        Merge after the branches sums them back.

        The rule reads **Key** (or `in` when Key is unwired — split notes by
        their own pitch):
        - **Ranges** — the split points At 1 / 2 / 3: below At 1 is branch 0,
          up to At 2 branch 1, and so on (pitch zones, velocity layers, a 0/1
          condition with At 1 = 0.5);
        - **Modulo** — the key, rounded, mod Branches (wire Order: round robin);
        - **Rank** — the member with the highest key alive is branch 0, the next
          branch 1, … (the top note gets the lead, the rest the pad).

        **Decide** at birth (the branch is kept for the member's life) or always
        (re-decided every block — a member can move). Rank at birth compares
        with the members born before it only, so members born in one block
        are sorted the same whatever order the plan runs them in. A decision reads the key
        at the end of the block, so a member born mid-block is sorted by its own
        key, not the one before its birth. `in` passes through to its member's
        branch, with its quantity (a Pitch stays a Pitch). */
    class LifeSplitNode : public InheritingPortsNode
    {
    public:
        static constexpr int maxBranches = 4;
        enum class Rule { Ranges, Modulo, Rank };

        LifeSplitNode() noexcept : InheritingPortsNode (Quantity::Dimensionless) {}

        void reset() override { branch = -1; }

        int getNumInputPorts() const noexcept override { return 2; }
        int getNumOutputPorts() const noexcept override { return maxBranches + 1; }
        juce::String getTitle() const override { return "Split"; }
        juce::String getCategory() const override { return "Life-cycle"; }

        void resolveIncomingPort (const juce::String& toPortId, const PortDescriptor& source) noexcept override
        {
            if (toPortId == "in")
                offer (0, source, false);
        }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "in", .type = SignalType::Signal, .label = "In", .quantity = resolvedQuantity,
                                 .polymorphism = PortPolymorphism::Quantity },
                PortDescriptor { .id = "key", .type = SignalType::Signal, .label = "Key", .hasFallbackWhenUnconnected = true,
                                 .quantity = Quantity::Dimensionless },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            std::vector<PortDescriptor> ports;
            for (int k = 0; k < maxBranches; ++k)
                ports.push_back ({ .id = "branch." + juce::String (k), .type = SignalType::Signal,
                                   .label = "Branch " + juce::String (k + 1), .isPrimaryOutput = k == 0,
                                   .quantity = resolvedQuantity, .polymorphism = PortPolymorphism::Quantity });
            ports.push_back ({ .id = "branch", .type = SignalType::Signal, .label = "Branch", .isInteger = true,
                               .quantity = Quantity::Count, .step = 1.0f });
            return ports;
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            const auto point = [] (int i, float value)
            {
                return ParameterDescriptor { .id = "life.split.at" + juce::String (i), .minValue = -1000.0f, .maxValue = 1000.0f,
                                             .defaultValue = value, .displayName = "At " + juce::String (i) };
            };
            return {
                ParameterDescriptor { .id = "life.split.rule", .minValue = 0.0f, .maxValue = 2.0f, .defaultValue = 0.0f,
                                      .displayName = "Rule", .isInteger = true, .kind = ValueKind::Enum,
                                      .enumOptions = { { "ranges", "Ranges" }, { "modulo", "Modulo" }, { "rank", "Rank" } } },
                ParameterDescriptor { .id = "life.split.branches", .minValue = 2.0f, .maxValue = (float) maxBranches, .defaultValue = 2.0f,
                                      .displayName = "Branches", .isInteger = true, .quantity = Quantity::Count, .step = 1.0f },
                point (1, 60.0f), point (2, 72.0f), point (3, 84.0f),
                ParameterDescriptor { .id = "life.split.decide", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                      .displayName = "Decide", .isInteger = true, .kind = ValueKind::Enum,
                                      .enumOptions = { { "birth", "At birth" }, { "always", "Always" } } },
            };
        }

        void setParameter (const juce::String& id, float value) override
        {
            if (id == "life.split.rule")
                rule = (Rule) juce::jlimit (0, 2, (int) std::lround (value));
            else if (id == "life.split.branches")
                branches = juce::jlimit (2, maxBranches, (int) std::lround (value));
            else if (id == "life.split.at1")
                points[0] = value;
            else if (id == "life.split.at2")
                points[1] = value;
            else if (id == "life.split.at3")
                points[2] = value;
            else if (id == "life.split.decide")
                always = value > 0.5f;
        }

        /** Rank compares members: every copy of one Split shares a key table
            (one entry per instance), handed over after compile. */
        void setShared (InstanceGroupRuntime* runtimeIn, float* keyTableIn, int instanceIn) noexcept
        {
            runtime = runtimeIn;
            keyTable = keyTableIn;
            instance = instanceIn;
        }

        /** The branch this member is in (-1 before it is decided). What the plan
            reads to run or skip a branch's steps. */
        int getBranch() const noexcept { return branch; }

        bool supportsPerSample() const noexcept override { return false; }

        void processBlock (const float* const* inputs, float* const* outputs, int numSamples) noexcept override
        {
            const auto* in = inputs[0];
            if (numSamples > 0)
            {
                // An unwired Key reads NaN: sort by `in` itself.
                const auto last = numSamples - 1;
                const auto key = std::isnan (inputs[1][last]) ? in[last] : inputs[1][last];
                if (keyTable != nullptr)
                    keyTable[instance] = key;
                if (branch < 0 || always)
                    branch = decide (key);
            }

            for (int k = 0; k < maxBranches; ++k)
                for (int s = 0; s < numSamples; ++s)
                    outputs[k][s] = k == branch ? in[s] : 0.0f;
            for (int s = 0; s < numSamples; ++s)
                outputs[maxBranches][s] = (float) branch;
        }

    private:
        int decide (float key) const noexcept
        {
            switch (rule)
            {
                case Rule::Ranges:
                {
                    auto b = 0;
                    for (int i = 0; i < branches - 1; ++i)
                        if (key >= points[(size_t) i])
                            b = i + 1;
                    return b;
                }
                case Rule::Modulo:
                {
                    const auto n = (long) std::lround (key);
                    return (int) (((n % branches) + branches) % branches);
                }
                case Rule::Rank:
                {
                    // Against the members alive (born before this one, when
                    // deciding at birth); a tie goes to the older one.
                    auto above = 0;
                    if (runtime != nullptr && keyTable != nullptr)
                    {
                        const auto& orders = runtime->birthOrder;
                        const auto mine = orders[(size_t) instance];
                        for (int j = 0; j < runtime->count; ++j)
                        {
                            if (j == instance || ! runtime->instances[(size_t) j].run)
                                continue;
                            const auto older = orders[(size_t) j] < mine;
                            if (! always && ! older)
                                continue;
                            if (keyTable[j] > key || (keyTable[j] == key && older))
                                ++above;
                        }
                    }
                    return juce::jmin (above, branches - 1);
                }
            }
            return 0;
        }

        Rule rule = Rule::Ranges;
        int branches = 2;
        std::array<float, 3> points { 60.0f, 72.0f, 84.0f };
        bool always = false;
        int branch = -1;
        InstanceGroupRuntime* runtime = nullptr;
        float* keyTable = nullptr;
        int instance = 0;
    };
}
