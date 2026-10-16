#pragma once

#include "bazalt/engine/graph/InstanceGroups.h"
#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include "bazalt/engine/nodes/InstanceSeeding.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <juce_core/juce_core.h>

namespace bazalt::engine::nodes
{
    /** Stable type id: "life.population" — wiki/plans/LifeCycle.md 2b.2. The one
        allocator: everything downstream of it runs once per member, up to Size
        members at a time.

        - **Born** on a note (Notes input; MIDI through io.noteIn), on an event
          (Born input; a rate is an Events node wired in), or **Always** (all Size
          members alive from the start).
        - **Lives** while held (until note-off or a Release event) / for Duration /
          forever. After its gate falls a member keeps sounding until it goes
          silent, then its slot is free again.
        - **When full:** steal the oldest / steal the quietest / ignore the birth.
        - Every member knows who it is: gate, pitch, velocity, index (its slot),
          order (the how-manyeth birth it was), age, alive (members alive now),
          random 1 / 2 and position (fixed for the member's life, from Seed and
          order), start / stop.

        Two roles in one class. Every copy the instance axis makes is a
        **member** (`pop@3`). When births come from inside the patch (no
        io.noteIn), the expansion adds one more copy, the **birth** copy
        (`pop@birth`, `life.population.birthGroup` set): it alone hears the
        inputs, and at the exact sample something is born it asks the driver for
        a slot (InstanceGroupRuntime::birthSink), which starts that member at
        that sample (spawnAt). A hidden cable from the birth copy (`births`) to
        every member (`afterBirths`) makes the birth copy run first in each block. With MIDI,
        each member hears its own copy of the note-in, exactly like a voice. */
    class LifePopulationNode : public Node
    {
    public:
        enum Input { notesIn, bornIn, releaseIn, pitchIn, velocityIn, durationIn, afterBirthsIn, numInputs };
        enum Output { gateOut, pitchOut, velocityOut, indexOut, orderOut, ageOut, aliveOut, random1Out, random2Out, positionOut,
                      startOut, stopOut, birthsOut, numOutputs };
        enum class Lives { Hold, Duration, Forever };
        enum class WhenFull { StealOldest, StealQuietest, Ignore };

        void prepare (const NodePrepareInfo& info) override { sampleRate = info.sampleRate > 0.0 ? info.sampleRate : 48000.0; }

        void reset() override
        {
            gate = false;
            ageSamples = 0;
            durationSamples = 0;
            startEvent = stopEvent = false;
            pendingSpawn = -1;
            pendingRelease = -1;
            alwaysBorn = false;
            for (auto& key : heldKeys)
                key = -1.0f;
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }
        juce::String getTitle() const override { return "Population"; }
        juce::String getCategory() const override { return "Life-cycle"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "notes", .type = SignalType::Note, .label = "Notes" },
                PortDescriptor { .id = "born", .type = SignalType::Event, .label = "Born" },
                PortDescriptor { .id = "release", .type = SignalType::Event, .label = "Release" },
                PortDescriptor { .id = "life.population.pitch", .type = SignalType::Signal, .label = "Pitch", .unit = "st",
                                 .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = 60.0f, .hasFallbackWhenUnconnected = true,
                                 .quantity = Quantity::Pitch },
                PortDescriptor { .id = "life.population.velocity", .type = SignalType::Signal, .label = "Velocity",
                                 .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .hasFallbackWhenUnconnected = true,
                                 .quantity = Quantity::Unipolar, .polarity = Polarity::Unipolar },
                PortDescriptor { .id = "life.population.duration", .type = SignalType::Signal, .label = "Duration", .unit = "s",
                                 .minValue = 0.001f, .maxValue = 30.0f, .defaultValue = 0.1f, .isLogScale = true,
                                 .hasFallbackWhenUnconnected = true, .quantity = Quantity::Time, .curve = Curve::Logarithmic },
                PortDescriptor { .id = "afterBirths", .type = SignalType::Event, .hidden = true },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "gate", .type = SignalType::Signal, .label = "Gate", .isPrimaryOutput = true, .quantity = Quantity::Boolean },
                PortDescriptor { .id = "pitch", .type = SignalType::Signal, .label = "Pitch", .unit = "st", .minValue = 0.0f, .maxValue = 127.0f,
                                 .defaultValue = 60.0f, .quantity = Quantity::Pitch },
                PortDescriptor { .id = "velocity", .type = SignalType::Signal, .label = "Velocity", .quantity = Quantity::Unipolar,
                                 .polarity = Polarity::Unipolar },
                PortDescriptor { .id = "index", .type = SignalType::Signal, .label = "Index", .isInteger = true, .quantity = Quantity::Count, .step = 1.0f },
                PortDescriptor { .id = "order", .type = SignalType::Signal, .label = "Order", .isInteger = true, .quantity = Quantity::Count, .step = 1.0f },
                PortDescriptor { .id = "age", .type = SignalType::Signal, .label = "Age", .unit = "s", .quantity = Quantity::Time },
                PortDescriptor { .id = "alive", .type = SignalType::Signal, .label = "Alive", .isInteger = true, .quantity = Quantity::Count, .step = 1.0f },
                PortDescriptor { .id = "random1", .type = SignalType::Signal, .label = "Random 1", .quantity = Quantity::Bipolar, .polarity = Polarity::Bipolar },
                PortDescriptor { .id = "random2", .type = SignalType::Signal, .label = "Random 2", .quantity = Quantity::Bipolar, .polarity = Polarity::Bipolar },
                PortDescriptor { .id = "position", .type = SignalType::Signal, .label = "Position", .quantity = Quantity::Bipolar, .polarity = Polarity::Bipolar },
                PortDescriptor { .id = "start", .type = SignalType::Event, .label = "Start" },
                PortDescriptor { .id = "stop", .type = SignalType::Event, .label = "Stop" },
                PortDescriptor { .id = "births", .type = SignalType::Event, .hidden = true },
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "life.population.size", .minValue = 1.0f, .maxValue = (float) maxInstancesPerGroup,
                                      .defaultValue = 8.0f, .displayName = "Size", .isInteger = true, .quantity = Quantity::Count,
                                      .step = 1.0f, .isStructural = true },
                ParameterDescriptor { .id = "life.population.born", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                      .displayName = "Born", .isInteger = true, .kind = ValueKind::Enum,
                                      .enumOptions = { { "input", "On input" }, { "always", "Always" } }, .isStructural = true },
                ParameterDescriptor { .id = "life.population.lives", .minValue = 0.0f, .maxValue = 2.0f, .defaultValue = 0.0f,
                                      .displayName = "Lives", .isInteger = true, .kind = ValueKind::Enum,
                                      .enumOptions = { { "hold", "While held" }, { "duration", "For Duration" }, { "forever", "Forever" } } },
                ParameterDescriptor { .id = "life.population.whenFull", .minValue = 0.0f, .maxValue = 2.0f, .defaultValue = 0.0f,
                                      .displayName = "When full", .isInteger = true, .kind = ValueKind::Enum,
                                      .enumOptions = { { "oldest", "Steal oldest" }, { "quietest", "Steal quietest" }, { "ignore", "Ignore" } },
                                      .isStructural = true },
                ParameterDescriptor { .id = "life.population.seed", .minValue = 0.0f, .maxValue = 999999.0f, .defaultValue = 1.0f,
                                      .displayName = "Seed", .isInteger = true, .quantity = Quantity::Count, .step = 1.0f, .isStructural = true },
            };
        }

        void setParameter (const juce::String& id, float value) override
        {
            const auto asInt = (int) std::lround (value);
            if (id == "life.population.size")
                size = juce::jlimit (1, maxInstancesPerGroup, asInt);
            else if (id == "life.population.born")
                always = asInt == 1;
            else if (id == "life.population.lives")
                lives = (Lives) juce::jlimit (0, 2, asInt);
            else if (id == "life.population.whenFull")
                whenFull = (WhenFull) juce::jlimit (0, 2, asInt);
            else if (id == "life.population.seed")
                seed = asInt;
            else if (id == "life.population.pitch")
                storedPitch = value;
            else if (id == "life.population.velocity")
                storedVelocity = value;
            else if (id == "life.population.duration")
                storedDuration = value;
            else if (id == "life.population.birthGroup") // internal: set by the instance expansion on the birth copy
                birthGroup = asInt - 1;
            else if (id == "life.population.parentInstance") // internal: a nested birth copy's parent member
                parentInstance = asInt;
        }

        int getSize() const noexcept { return size; }
        bool isAlways() const noexcept { return always; }
        Lives getLives() const noexcept { return always ? Lives::Forever : lives; }
        WhenFull getWhenFull() const noexcept { return whenFull; }
        bool isBirthCopy() const noexcept { return birthGroup >= 0; }
        int getBirthGroup() const noexcept { return birthGroup; }
        void setRuntime (InstanceGroupRuntime* runtimeIn) noexcept { runtime = runtimeIn; }

        // The driver starts a member (after a steal's fade, or a MIDI note) and ends it.
        bool getGate() const noexcept { return gate; }
        float getPitch() const noexcept { return pitch; }
        float getVelocity() const noexcept { return velocity; }
        void spawnInstance (float pitchIn, float velocityIn) noexcept { spawnAt (0, pitchIn, velocityIn, nextDuration, nextOrder); }
        void releaseInstance() noexcept { releaseAt (0); }
        void releaseAt (int offset) noexcept { pendingRelease = juce::jmax (0, offset); }

        /** Starts this member at `offset` samples into the coming block.
            `durationSeconds` < 0: read this copy's own Duration input then. */
        void spawnAt (int offset, float pitchIn, float velocityIn, float durationSeconds, int orderIn) noexcept
        {
            pendingSpawn = juce::jmax (0, offset);
            pendingPitch = pitchIn;
            pendingVelocity = velocityIn;
            pendingDuration = durationSeconds;
            pendingOrder = orderIn;
        }

        /** What the next start the driver makes directly carries (a MIDI note,
            or a birth that waited for a steal's fade). `durationSeconds` < 0:
            this copy's own Duration input. */
        void setNextBirth (int orderIn, float durationSeconds) noexcept
        {
            nextOrder = orderIn;
            nextDuration = durationSeconds;
        }
        float getRandom1() const noexcept { return random1; }
        int getOrder() const noexcept { return order; }
        float getRandom2() const noexcept { return random2; }

        void consumeNoteBlock (const NoteEvent* input, int numSamples) noexcept override
        {
            noteBlock = input;
            noteBlockLength = numSamples;
        }

        void processBlock (const float* const* inputs, float* const* outputs, int numSamples) noexcept override
        {
            if (isBirthCopy())
                processBirths (inputs, numSamples);

            const auto alive = runtime != nullptr ? (float) runtime->alive : (gate ? 1.0f : 0.0f);
            for (int s = 0; s < numSamples; ++s)
            {
                if (! isBirthCopy() && noteBlock != nullptr && s < noteBlockLength)
                {
                    // MIDI: this member's own note-in copy.
                    const auto& note = noteBlock[s];
                    if (note.startEvent)
                        start (note.pitch, note.velocity, nextDuration >= 0.0f ? nextDuration : readDuration (inputs, s), nextOrder);
                    if (note.stopEvent && getLives() == Lives::Hold)
                        stop();
                    if (gate)
                        pitch = note.pitch; // bend follows the held note
                }
                if (s == pendingSpawn)
                {
                    start (pendingPitch, pendingVelocity, pendingDuration >= 0.0f ? pendingDuration : readDuration (inputs, s), pendingOrder);
                    pendingSpawn = -1;
                }
                if (s == pendingRelease)
                {
                    stop();
                    pendingRelease = -1;
                }
                if (gate && getLives() == Lives::Duration && ageSamples >= durationSamples)
                    stop();

                outputs[gateOut][s] = gate ? 1.0f : 0.0f;
                outputs[pitchOut][s] = pitch;
                outputs[velocityOut][s] = velocity;
                outputs[indexOut][s] = (float) slot;
                outputs[orderOut][s] = (float) order;
                outputs[ageOut][s] = (float) ((double) ageSamples / sampleRate);
                outputs[aliveOut][s] = alive;
                outputs[random1Out][s] = random1;
                outputs[random2Out][s] = random2;
                outputs[positionOut][s] = position;
                outputs[startOut][s] = startEvent ? 1.0f : 0.0f;
                outputs[stopOut][s] = stopEvent ? 1.0f : 0.0f;
                outputs[birthsOut][s] = 0.0f;
                ++ageSamples;
                startEvent = stopEvent = false;
            }
            // A release asked for past this block (a driver's releaseInstance) lands at its start.
            if (pendingRelease >= numSamples)
                pendingRelease = 0;
            if (pendingSpawn >= numSamples)
                pendingSpawn = 0;
            noteBlock = nullptr;
        }

        bool supportsPerSample() const noexcept override { return false; }

    private:
        static float read (const float* const* inputs, int index, int s, float fallback) noexcept
        {
            const auto v = inputs != nullptr && inputs[index] != nullptr ? inputs[index][s] : fallback;
            return std::isnan (v) ? fallback : v;
        }
        float readDuration (const float* const* inputs, int s) const noexcept
        {
            return juce::jmax (0.0f, read (inputs, durationIn, s, storedDuration));
        }

        void start (float pitchIn, float velocityIn, float durationSeconds, int orderIn) noexcept
        {
            gate = true;
            pitch = pitchIn;
            velocity = velocityIn;
            ageSamples = 0;
            durationSamples = (int64_t) std::llround ((double) durationSeconds * sampleRate);
            order = orderIn;
            startEvent = true;
            if (runtime != nullptr && slot < (int) runtime->birthOrder.size())
                runtime->birthOrder[(size_t) slot] = order;
            auto random = juce::Random (combineInstanceSeed (seed, order));
            random1 = random.nextFloat() * 2.0f - 1.0f;
            random2 = random.nextFloat() * 2.0f - 1.0f;
            position = random.nextFloat() * 2.0f - 1.0f;
        }

        void stop() noexcept
        {
            if (! gate)
                return;
            gate = false;
            stopEvent = true;
        }

        /** The birth copy: find every birth and release in this block, at its sample. */
        void processBirths (const float* const* inputs, int numSamples) noexcept
        {
            if (runtime == nullptr || runtime->birthSink == nullptr)
                return;
            auto& sink = *runtime->birthSink;

            if (always)
            {
                if (! alwaysBorn)
                    for (int i = 0; i < size; ++i)
                        sink.born (*runtime, 0, storedPitch, storedVelocity, -1.0f, parentInstance);
                alwaysBorn = true;
                return;
            }

            for (int s = 0; s < numSamples; ++s)
            {
                const auto birthPitch = read (inputs, pitchIn, s, storedPitch);
                const auto birthVelocity = read (inputs, velocityIn, s, storedVelocity);
                const auto duration = readDuration (inputs, s);

                if (noteBlock != nullptr && s < noteBlockLength)
                {
                    const auto& note = noteBlock[s];
                    if (note.stopEvent)
                        releaseHeld (sink, s, note.pitch);
                    if (note.startEvent)
                        remember (sink.born (*runtime, s, note.pitch, note.velocity, duration, parentInstance), note.pitch);
                }
                if (read (inputs, bornIn, s, 0.0f) > 0.0f)
                    remember (sink.born (*runtime, s, birthPitch, birthVelocity, duration, parentInstance), -2.0f);
                if (read (inputs, releaseIn, s, 0.0f) > 0.0f)
                    releaseHeld (sink, s, -3.0f);
            }
        }

        void remember (int instance, float key) noexcept
        {
            if (instance >= 0 && instance < maxInstancesPerGroup)
                heldKeys[(size_t) instance] = key;
        }

        /** `key` -3: every held member; else the members holding that note. */
        void releaseHeld (InstanceBirthSink& sink, int offset, float key) noexcept
        {
            if (getLives() != Lives::Hold)
                return;
            for (int i = 0; i < maxInstancesPerGroup; ++i)
            {
                auto& held = heldKeys[(size_t) i];
                if (held == -1.0f || (key != -3.0f && std::abs (held - key) > 0.01f))
                    continue;
                sink.release (*runtime, i, offset);
                held = -1.0f;
            }
        }

    public:
        /** Which member this copy is (set once after compile). */
        void setSlot (int slotIn) noexcept
        {
            slot = slotIn;
        }

    private:
        double sampleRate = 48000.0;
        int size = 8, seed = 1, birthGroup = -1, parentInstance = -1;
        bool always = false;
        Lives lives = Lives::Hold;
        WhenFull whenFull = WhenFull::StealOldest;
        float storedPitch = 60.0f, storedVelocity = 1.0f, storedDuration = 0.1f;
        InstanceGroupRuntime* runtime = nullptr;

        // A member's life.
        bool gate = false, startEvent = false, stopEvent = false;
        float pitch = 60.0f, velocity = 0.0f, random1 = 0.0f, random2 = 0.0f, position = 0.0f;
        int64_t ageSamples = 0, durationSamples = 0;
        int slot = 0, order = 0, nextOrder = 0;
        int pendingSpawn = -1, pendingRelease = -1, pendingOrder = 0;
        float pendingPitch = 60.0f, pendingVelocity = 1.0f, pendingDuration = -1.0f, nextDuration = -1.0f;
        const NoteEvent* noteBlock = nullptr;
        int noteBlockLength = 0;

        // The birth copy: which note each member it started holds.
        std::array<float, maxInstancesPerGroup> heldKeys {};
        bool alwaysBorn = false;
    };
}
