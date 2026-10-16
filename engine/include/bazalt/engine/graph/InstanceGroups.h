#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>

namespace bazalt::engine
{
    class Node;

    /** The most instances one allocator may run (wiki/plans/LifeCycle.md 2b):
        each one is a copy of its chain, so the real limit is CPU — a grain of
        a few nodes can run a hundred times, a full synth voice cannot. The
        driver's pools are sized to this once, in prepare(). */
    constexpr int maxInstancesPerGroup = 128;

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

    struct InstanceGroupRuntime;

    /** Who starts a member when something is born inside the patch (Population's
        birth copy calls it on the audio thread, mid-plan, at the sample of the
        birth). The driver picks a slot (stealing per the group's policy),
        resets that member, marks it running and starts it at `offset`. Returns
        the slot, or -1 when the birth is ignored or waits for a steal's fade. */
    struct InstanceBirthSink
    {
        virtual ~InstanceBirthSink() = default;
        /** `parentInstance`: for a nested Population (2b.5), the parent member
            whose own slice of members this birth goes into; -1 at the top. */
        virtual int born (InstanceGroupRuntime& group, int offset, float pitch, float velocity, float durationSeconds,
                          int parentInstance) noexcept = 0;
        virtual void release (InstanceGroupRuntime& group, int instance, int offset) noexcept = 0;
    };

    struct InstanceGroupRuntime
    {
        juce::String originId;           // the allocator node's id in the editable graph
        int count = 0;                   // instances in this group
        int bundleSlot = -1;             // which of the driver's per-origin bundles drives it (set by the driver)
        int groupIndex = 0;              // this group's index in the plan
        int parentGroup = -1;            // 2b.5: the group it lives inside (-1 at the top)
        int membersPerParent = 0;        // ... and its members per member of that group (its Size)
        int alive = 0;                   // members alive now (written by the driver, read by Population's Alive)
        int births = 0;                  // births so far: the next member's order (the driver counts)
        std::vector<int> birthOrder;     // per instance: the order it was born with (Population writes it)
        InstanceBirthSink* birthSink = nullptr; // set by the driver before the plan is published
        /** Population's birth copy when births come from inside the patch
            (nullptr otherwise): the group's members start only through it. */
        Node* birthNode = nullptr;
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
