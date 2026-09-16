#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "bazalt/engine/patch/PatchDocument.h"
#include "bazalt/engine/SmoothedParameter.h"
#include "bazalt/engine/graph/ExecutionPlan.h"
#include <array>
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

        void setMappings (std::vector<bazalt::engine::MacroMapping> newMappings) { mappings = std::move (newMappings); }
        const std::vector<bazalt::engine::MacroMapping>& getMappings() const noexcept { return mappings; }

        std::vector<float> getCurrentValues() const;
        void setValuesForLoadedPatch (const std::vector<float>& values);

        /** Advances each macro's smoother by numSamples and applies the
            result to every mapped target across every plan given. Call
            once per processBlock, before rendering any voice. Takes a raw
            pointer + count (not std::vector) deliberately — this runs on
            the audio thread and must not allocate.
        */
        void applyToPlans (bazalt::engine::ExecutionPlan* const* plans, int numPlans, int numSamples) noexcept;

    private:
        std::array<juce::AudioParameterFloat*, numMacros> parameters {};
        std::array<bazalt::engine::SmoothedParameter, numMacros> smoothers;
        std::vector<bazalt::engine::MacroMapping> mappings;
    };
}
