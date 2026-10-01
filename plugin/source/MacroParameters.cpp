#include "MacroParameters.h"

namespace bazalt
{
    void MacroParameters::addParametersTo (juce::AudioProcessor& processor)
    {
        for (int i = 0; i < numMacros; ++i)
        {
            auto id = "macro" + juce::String (i + 1);
            auto name = "Macro " + juce::String (i + 1);

            auto* param = new juce::AudioParameterFloat (juce::ParameterID (id, 1), name, 0.0f, 1.0f, 0.0f);
            processor.addParameter (param);
            parameters[(size_t) i] = param;
        }
    }

    void MacroParameters::prepare (double sampleRate)
    {
        for (auto& smoother : smoothers)
        {
            smoother.prepare (sampleRate, 0.02);
            smoother.reset (0.0f);
        }
    }

    void MacroParameters::setMappings (std::vector<bazalt::engine::MacroMapping> newMappings)
    {
        const auto current = currentMappingBuffer.load (std::memory_order_acquire);
        const auto next = (current + 1) % (int) mappingBuffers.size();

        auto& buffer = mappingBuffers[(size_t) next];
        const auto count = juce::jmin ((int) newMappings.size(), numMacros);
        for (int i = 0; i < count; ++i)
            buffer.entries[(size_t) i] = std::move (newMappings[(size_t) i]);
        buffer.count = count;

        currentMappingBuffer.store (next, std::memory_order_release);
    }

    std::vector<bazalt::engine::MacroMapping> MacroParameters::getMappings() const
    {
        const auto slot = currentMappingBuffer.load (std::memory_order_acquire);
        const auto& buffer = mappingBuffers[(size_t) slot];
        return { buffer.entries.begin(), buffer.entries.begin() + buffer.count };
    }

    std::vector<float> MacroParameters::getCurrentValues() const
    {
        std::vector<float> values ((size_t) numMacros);
        for (int i = 0; i < numMacros; ++i)
            values[(size_t) i] = parameters[(size_t) i]->get();
        return values;
    }

    void MacroParameters::setValuesForLoadedPatch (const std::vector<float>& values)
    {
        for (int i = 0; i < numMacros && i < (int) values.size(); ++i)
        {
            *parameters[(size_t) i] = values[(size_t) i];
            smoothers[(size_t) i].reset (values[(size_t) i]);
        }
    }

    void MacroParameters::advanceSmoothers (int numSamples) noexcept
    {
        for (int macroIndex = 0; macroIndex < numMacros; ++macroIndex)
        {
            auto& smoother = smoothers[(size_t) macroIndex];
            smoother.setTargetValue (parameters[(size_t) macroIndex]->get());
            smoother.skip (numSamples);
        }
    }

    void MacroParameters::applyToPlans (bazalt::engine::ExecutionPlan* const* plans, int numPlans, int /*numSamples*/) noexcept
    {
        const auto slot = currentMappingBuffer.load (std::memory_order_acquire);
        const auto& buffer = mappingBuffers[(size_t) slot];

        for (int mappingIndex = 0; mappingIndex < buffer.count; ++mappingIndex)
        {
            const auto& mapping = buffer.entries[(size_t) mappingIndex];
            if (mapping.macroIndex < 0 || mapping.macroIndex >= numMacros)
                continue; // defensive - deriveMacroMappings never produces this, but this is RT code

            const auto value = smoothers[(size_t) mapping.macroIndex].getCurrentValue();
            const auto targetValue = mapping.rangeMin + value * (mapping.rangeMax - mapping.rangeMin);

            for (int i = 0; i < numPlans; ++i)
                if (plans[i] != nullptr)
                    if (auto* node = plans[i]->getNodeById (mapping.targetNodeId))
                        node->setParameter (mapping.targetParameterId, targetValue);
        }
    }
}
