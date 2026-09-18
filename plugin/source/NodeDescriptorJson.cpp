#include "NodeDescriptorJson.h"

namespace bazalt
{
    namespace
    {
        using bazalt::engine::Curve;
        using bazalt::engine::NodeLayoutVariant;
        using bazalt::engine::ParameterDescriptor;
        using bazalt::engine::Polarity;
        using bazalt::engine::PortDescriptor;
        using bazalt::engine::PortGroup;
        using bazalt::engine::Quantity;
        using bazalt::engine::SignalType;
        using bazalt::engine::ValueKind;

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
                case SignalType::Data:     return "data";
            }
            jassertfalse;
            return "control";
        }

        juce::String dataTagToString (bazalt::engine::DataTag tag)
        {
            switch (tag)
            {
                case bazalt::engine::DataTag::Unknown:        return "unknown";
                case bazalt::engine::DataTag::Curve:          return "curve";
                case bazalt::engine::DataTag::Scale:          return "scale";
                case bazalt::engine::DataTag::Wavetable:      return "wavetable";
                case bazalt::engine::DataTag::ModalSet:       return "modal-set";
                case bazalt::engine::DataTag::Sample:         return "sample";
                case bazalt::engine::DataTag::ImpulseResponse: return "ir";
            }
            jassertfalse;
            return "unknown";
        }

        juce::String channelsToString (bazalt::engine::Channels channels)
        {
            switch (channels)
            {
                case bazalt::engine::Channels::Mono:      return "mono";
                case bazalt::engine::Channels::Stereo:    return "stereo";
                case bazalt::engine::Channels::Inherited: return "inherited";
            }
            jassertfalse;
            return "mono";
        }

        juce::var dataTagsToVar (const std::vector<bazalt::engine::DataTag>& tags)
        {
            juce::Array<juce::var> array;
            array.ensureStorageAllocated ((int) tags.size());
            for (const auto& tag : tags)
                array.add (dataTagToString (tag));
            return juce::var (array);
        }

        // ---- Value contract (M14, VALUE_MODEL.md) ------------------------
        juce::String valueKindToString (ValueKind kind)
        {
            switch (kind)
            {
                case ValueKind::Float: return "float";
                case ValueKind::Int:   return "int";
                case ValueKind::Bool:  return "bool";
                case ValueKind::Enum:  return "enum";
            }
            jassertfalse;
            return "float";
        }

        juce::String quantityToString (Quantity quantity)
        {
            switch (quantity)
            {
                case Quantity::Dimensionless: return "dimensionless";
                case Quantity::Frequency:     return "frequency";
                case Quantity::Pitch:         return "pitch";
                case Quantity::Time:          return "time";
                case Quantity::Gain:          return "gain";
                case Quantity::Ratio:         return "ratio";
                case Quantity::Unipolar:      return "unipolar";
                case Quantity::Bipolar:       return "bipolar";
                case Quantity::Count:         return "count";
                case Quantity::Phase:         return "phase";
            }
            jassertfalse;
            return "dimensionless";
        }

        juce::String curveToString (Curve curve)
        {
            switch (curve)
            {
                case Curve::Linear:      return "linear";
                case Curve::Exponential: return "exponential";
                case Curve::Logarithmic: return "logarithmic";
                case Curve::CustomRef:   return "custom-ref";
            }
            jassertfalse;
            return "linear";
        }

        juce::String polarityToString (Polarity polarity)
        {
            switch (polarity)
            {
                case Polarity::Unipolar: return "unipolar";
                case Polarity::Bipolar:  return "bipolar";
            }
            jassertfalse;
            return "unipolar";
        }

        juce::var enumOptionsToVar (const std::vector<bazalt::engine::EnumOption>& options)
        {
            juce::Array<juce::var> array;
            array.ensureStorageAllocated ((int) options.size());
            for (const auto& option : options)
            {
                auto* obj = new juce::DynamicObject();
                obj->setProperty ("id", option.id);
                obj->setProperty ("label", option.label);
                array.add (juce::var (obj));
            }
            return juce::var (array);
        }

        // null when the port isn't part of a growable group (the common
        // case) — same "absent means absent, never a fake default" rule
        // optionalFloatToVar already follows, so the UI can tell "no
        // group" from "a group with a 0-wide range" unambiguously.
        juce::var portGroupToVar (const std::optional<PortGroup>& group)
        {
            if (! group.has_value())
                return {};

            auto* obj = new juce::DynamicObject();
            obj->setProperty ("idPrefix", group->idPrefix);
            obj->setProperty ("minCount", group->minCount);
            obj->setProperty ("maxCount", group->maxCount);
            obj->setProperty ("autoRevealOnLastConnected", group->autoRevealOnLastConnected);
            return juce::var (obj);
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
            obj->setProperty ("hasFallbackWhenUnconnected", port.hasFallbackWhenUnconnected);
            obj->setProperty ("kind", valueKindToString (port.kind));
            obj->setProperty ("quantity", quantityToString (port.quantity));
            obj->setProperty ("curve", curveToString (port.curve));
            obj->setProperty ("polarity", polarityToString (port.polarity));
            obj->setProperty ("enumOptions", enumOptionsToVar (port.enumOptions));
            obj->setProperty ("step", port.step);
            obj->setProperty ("softMin", optionalFloatToVar (port.softMin));
            obj->setProperty ("softMax", optionalFloatToVar (port.softMax));
            obj->setProperty ("group", portGroupToVar (port.group));
            obj->setProperty ("dataTags", dataTagsToVar (port.dataTags));
            obj->setProperty ("channels", channelsToString (port.channels));
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
            obj->setProperty ("isInteger", parameter.isInteger);
            obj->setProperty ("kind", valueKindToString (parameter.kind));
            obj->setProperty ("quantity", quantityToString (parameter.quantity));
            obj->setProperty ("curve", curveToString (parameter.curve));
            obj->setProperty ("polarity", polarityToString (parameter.polarity));
            obj->setProperty ("enumOptions", enumOptionsToVar (parameter.enumOptions));
            obj->setProperty ("step", parameter.step);
            obj->setProperty ("softMin", optionalFloatToVar (parameter.softMin));
            obj->setProperty ("softMax", optionalFloatToVar (parameter.softMax));
            obj->setProperty ("isStructural", parameter.isStructural);
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
