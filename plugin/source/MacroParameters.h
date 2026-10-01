#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "bazalt/engine/patch/PatchDocument.h"
#include "bazalt/engine/SmoothedParameter.h"
#include "bazalt/engine/graph/ExecutionPlan.h"
#include <array>
#include <atomic>
#include <vector>

namespace bazalt
{
    /** Owns the fixed pool of host-exposed macro parameters
        (ARCHITECTURE.md §4.3): plain juce::AudioParameterFloat, `Macro
        1..32`, each optionally mapped to one or more node-parameter
        targets across every active voice. A macro with no mapping is just
        an inert automatable float — cheap, matches "arbitrary, cheap to
        change" pool sizing. Node parameters with no macro mapping are
        still fully saved/restored via the patch, just not
        host-automatable.

        Each macro is smoothed (~20ms) before being applied, so a fast host
        automation move or a quick knob turn ramps the target parameter
        rather than stepping it — the click-free guarantee the M3 exit
        criteria ask for.
    */
    class MacroParameters
    {
    public:
        static constexpr int numMacros = 32;

        void addParametersTo (juce::AudioProcessor& processor);
        void prepare (double sampleRate);

        /** Message-thread only: `GraphEditController::recompileAndPublish()` calls
            this after every successful compile, with the mappings it just derived
            fresh from the live graph's own util.macro nodes. Publishes into the
            triple buffer below rather than reassigning a plain member - a raw
            std::vector moved/reassigned here (the shape this had from M3 through
            util.macro's own first landing) was a real audio-thread data race once
            util.macro became real: this now runs on every single graph-edit
            command (any addNode/connect/setParameterValue/...), not just once at
            construction the way the old 4-entry default table did, so the window
            for the audio thread to read a torn/mid-move vector stopped being
            theoretical. See applyToPlans()'s own comment for the read side.
        */
        void setMappings (std::vector<bazalt::engine::MacroMapping> newMappings);
        /** Message-thread only (tests/inspection) - NOT safe to call from the audio
            thread; allocates a fresh vector copy every call. */
        std::vector<bazalt::engine::MacroMapping> getMappings() const;

        std::vector<float> getCurrentValues() const;
        void setValuesForLoadedPatch (const std::vector<float>& values);

        /** For binding a macro to a UI control (M5: WebSliderRelay). Index
            is 0-based (macro 1 is index 0), matching MacroMapping::macroIndex.
        */
        juce::AudioParameterFloat& getParameter (int index) const noexcept { return *parameters[(size_t) index]; }

        /** Advances every macro's smoother by numSamples exactly once,
            targeting its current host parameter value. Call exactly ONCE
            per processBlock, regardless of how many domains/origin bundles
            are active that block — applyToPlans() below reads each
            smoother's already-advanced current value and does not itself
            advance anything, specifically so a block with N active domains
            doesn't tick the ~20ms ramp N times (a real bug this split
            fixes: calling the old combined applyToPlans() once per domain
            both over-advanced the ramp and let the SAME macro read two
            different interpolated values within one block, once per
            domain). Audio-thread, must not allocate.
        */
        void advanceSmoothers (int numSamples) noexcept;

        /** Applies each macro's CURRENT (already-advanced, see
            advanceSmoothers() above) smoothed value to every mapped target
            across every plan given. Call once per active domain/origin
            bundle per processBlock (after advanceSmoothers(), not instead
            of it). Takes a raw pointer + count (not std::vector)
            deliberately — this runs on the audio thread and must not
            allocate. A null entry in `plans` is a real, expected case (a
            voice's PlanSwapper hasn't been published to yet — e.g. the
            first few processBlock() calls can race the message thread's
            initial GraphEditController::prepare() publish, since JUCE
            doesn't guarantee prepareToPlay() has fully returned before the
            audio callback starts) and is silently skipped, matching
            renderVoiceRange()'s own null check — found by a real
            Standalone-app crash this null check was originally missing
            for.

            Audio-thread side of the triple-buffer handoff above: loads
            `currentMappingBuffer` ONCE at the top of the call (one atomic
            load, no allocation) and reads only that slot's entries for the
            whole call — never touches the slot setMappings() might be
            mid-write into.
        */
        void applyToPlans (bazalt::engine::ExecutionPlan* const* plans, int numPlans, int numSamples) noexcept;

    private:
        std::array<juce::AudioParameterFloat*, numMacros> parameters {};
        std::array<bazalt::engine::SmoothedParameter, numMacros> smoothers;

        /** Lock-free single-writer (message thread, setMappings())/single-reader
            (audio thread, applyToPlans()) publish for the live mapping list, via
            the exact same atomic-index-swap triple-buffer pattern
            TelemetryFrameBuffer.h already uses for its own message/audio-adjacent
            handoff (ARCHITECTURE.md §3.2/§6.2's own precedent) - picked over that
            class's literal type because a mapping is a small struct (two
            juce::Strings + two floats + an int), not raw bytes, so this assigns
            MacroMapping structs into a fixed slot instead of memcpy-ing one. At
            most `numMacros` mappings can ever exist (one per claimed macro slot;
            GraphEditController's own collision check rejects two macros ever
            sharing a slot) - sized to that exact bound, never growable, so
            setMappings() never needs to reallocate the buffer slots themselves,
            only the juce::Strings inside the entries it writes (message-thread
            side, not audio-thread).
        */
        struct MappingBuffer
        {
            std::array<bazalt::engine::MacroMapping, numMacros> entries {};
            int count = 0;
        };
        std::array<MappingBuffer, 3> mappingBuffers;
        std::atomic<int> currentMappingBuffer { 0 };
    };
}
