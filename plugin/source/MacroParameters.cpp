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

    void MacroParameters::applyToPlans (bazalt::engine::ExecutionPlan* const* plans, int numPlans, int numSamples) noexcept
    {
        for (int macroIndex = 0; macroIndex < numMacros; ++macroIndex)
        {
            auto& smoother = smoothers[(size_t) macroIndex];
            smoother.setTargetValue (parameters[(size_t) macroIndex]->get());
            const auto value = smoother.skip (numSamples);

            for (const auto& mapping : mappings)
            {
                if (mapping.macroIndex != macroIndex)
                    continue;

                const auto targetValue = mapping.rangeMin + value * (mapping.rangeMax - mapping.rangeMin);

                for (int i = 0; i < numPlans; ++i)
                    if (plans[i] != nullptr)
                        if (auto* node = plans[i]->getNodeById (mapping.targetNodeId))
                            node->setParameter (mapping.targetParameterId, targetValue);
            }
        }
    }
}
