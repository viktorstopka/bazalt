#include "NodeDescriptorJson.h"

namespace bazalt
{
    namespace
    {
        using bazalt::engine::NodeLayoutVariant;
        using bazalt::engine::ParameterDescriptor;
        using bazalt::engine::PortDescriptor;
        using bazalt::engine::SignalType;

        juce::String signalTypeToString (SignalType type)
        {
            switch (type)
            {
                case SignalType::Audio:    return "audio";
                case SignalType::Control:  return "control";
                case SignalType::Event:    return "event";
                case SignalType::Note:     return "note";
                case SignalType::Spectral: return "spectral";
                case SignalType::Boolean:  return "boolean";
            }
            jassertfalse;
            return "control";
        }

        juce::String layoutVariantToString (NodeLayoutVariant variant)
        {
            switch (variant)
            {
                case NodeLayoutVariant::Standard:    return "standard";
                case NodeLayoutVariant::Horizontal:  return "horizontal";
                case NodeLayoutVariant::Singleton:   return "singleton";
                case NodeLayoutVariant::Decoration:  return "decoration";
            }
            jassertfalse;
            return "standard";
        }

        // optional<float> -> JS null when unset (never 0, never an omitted
        // key — see NodeDescriptorJson.h's own comment on why that
        // distinction matters to the UI).
        juce::var optionalFloatToVar (const std::optional<float>& value)
        {
            return value.has_value() ? juce::var (*value) : juce::var();
        }

        juce::var portDescriptorToVar (const PortDescriptor& port)
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("id", port.id);
            obj->setProperty ("type", signalTypeToString (port.type));
            obj->setProperty ("label", port.label);
            obj->setProperty ("isPrimaryOutput", port.isPrimaryOutput);
            obj->setProperty ("unit", port.unit);
            obj->setProperty ("minValue", optionalFloatToVar (port.minValue));
            obj->setProperty ("maxValue", optionalFloatToVar (port.maxValue));
            obj->setProperty ("defaultValue", port.defaultValue);
            obj->setProperty ("isInteger", port.isInteger);
            obj->setProperty ("isLogScale", port.isLogScale);
            return juce::var (obj);
        }

        juce::var parameterDescriptorToVar (const ParameterDescriptor& parameter)
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("id", parameter.id);
            obj->setProperty ("minValue", parameter.minValue);
            obj->setProperty ("maxValue", parameter.maxValue);
            obj->setProperty ("defaultValue", parameter.defaultValue);
            obj->setProperty ("skew", parameter.skew);
            obj->setProperty ("unit", parameter.unit);
            obj->setProperty ("displayName", parameter.displayName);
            return juce::var (obj);
        }

        template <typename Descriptor, typename ToVarFn>
        juce::var descriptorListToVar (const std::vector<Descriptor>& descriptors, ToVarFn toVar)
        {
            juce::Array<juce::var> array;
            array.ensureStorageAllocated ((int) descriptors.size());
            for (const auto& descriptor : descriptors)
                array.add (toVar (descriptor));
            return juce::var (array);
        }
    }

    juce::var nodeDescriptorToVar (const bazalt::engine::NodeDescriptor& descriptor)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("typeId", descriptor.typeId);
        obj->setProperty ("title", descriptor.title);
        obj->setProperty ("category", descriptor.category);
        obj->setProperty ("icon", descriptor.icon);
        obj->setProperty ("layoutVariant", layoutVariantToString (descriptor.layoutVariant));
        obj->setProperty ("inputs", descriptorListToVar (descriptor.inputs, portDescriptorToVar));
        obj->setProperty ("outputs", descriptorListToVar (descriptor.outputs, portDescriptorToVar));
        obj->setProperty ("parameters", descriptorListToVar (descriptor.parameters, parameterDescriptorToVar));
        return juce::var (obj);
    }

    juce::var nodeDescriptorsToVar (const std::vector<bazalt::engine::NodeDescriptor>& descriptors)
    {
        return descriptorListToVar (descriptors, nodeDescriptorToVar);
    }
}
