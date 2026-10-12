#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>

namespace bazalt::engine
{
    class Node;

    /** The instance axis (wiki/plans/InstanceAxis.md): what one ExecutionPlan
        knows about the instances an allocator (a "group") runs. The plan holds
        one runtime per group; whoever drives the plan (PluginProcessor, a test)
        decides each instance's state before every process() call, on the audio
        thread, and reads back what the reductions measured.

        - **run**: the instance's steps execute (its nodes advance).
        - **audible**: it is summed by the group's reductions (Merge, or the
          output when the output itself is per-instance).
        - **fading**: summed with a linear ramp from gainStart to gainEnd over
          the first rampSamples of the call, then silent — a stolen voice
          fading out (VoiceManager's steal fade).
        - **peak**: written by the reductions — the loudest sample this
          instance contributed in the last call (before its gain), what voice
          release detection listens to.
    */
    struct InstanceState
    {
        bool run = false;
        bool audible = false;
        bool fading = false;
        float gainStart = 1.0f, gainEnd = 1.0f;
        int rampSamples = 0;
        float peak = 0.0f;
    };

    struct InstanceGroupRuntime
    {
        juce::String originId;           // the allocator node's id in the editable graph
        int count = 0;                   // instances in this group
        int bundleSlot = -1;             // which of the driver's per-origin bundles drives it (set by the driver)
        std::vector<InstanceState> instances;

        /** Which instance previews show (the most recently played one). */
        std::atomic<int> previewInstance { 0 };

        /** Per instance, resolved at compile time so the audio thread never
            looks a node up by name: the instance's copy of the allocator, and
            of io.noteIn when the group has one (nullptr otherwise). */
        std::vector<Node*> originNodes;
        std::vector<Node*> noteInNodes;

        /** Per instance: every node slot (all lanes) and every per-sample
            region step that belongs to it — what resetInstance() resets. */
        std::vector<std::vector<int>> nodeSlotsByInstance;
        std::vector<std::vector<int>> regionStepsByInstance;

        int audibleCount() const noexcept
        {
            int n = 0;
            for (const auto& s : instances)
                n += s.audible ? 1 : 0;
            return n;
        }
    };
}
