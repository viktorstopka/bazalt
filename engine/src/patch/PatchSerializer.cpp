#include "bazalt/engine/patch/PatchSerializer.h"
#include "bazalt/engine/graph/CurveData.h"
#include "bazalt/engine/patch/ExactFloat.h"
#include <cmath>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace bazalt::engine
{
    namespace
    {
        juce::var parametersToVar (const std::unordered_map<juce::String, float>& parameters)
        {
            auto* obj = new juce::DynamicObject();
            for (const auto& [key, value] : parameters)
                obj->setProperty (juce::Identifier (key), exactFloatVar (value));
            return juce::var (obj);
        }

        std::unordered_map<juce::String, float> parametersFromVar (const juce::var& value)
        {
            std::unordered_map<juce::String, float> result;
            if (auto* obj = value.getDynamicObject())
                for (const auto& prop : obj->getProperties())
                    result[prop.name.toString()] = (float) prop.value;
            return result;
        }

        juce::var propertiesToVar (const std::unordered_map<juce::String, juce::var>& properties)
        {
            auto* obj = new juce::DynamicObject();
            for (const auto& [key, value] : properties)
                obj->setProperty (juce::Identifier (key), value);
            return juce::var (obj);
        }

        std::unordered_map<juce::String, juce::var> propertiesFromVar (const juce::var& value)
        {
            std::unordered_map<juce::String, juce::var> result;
            if (auto* obj = value.getDynamicObject())
                for (const auto& prop : obj->getProperties())
                    result[prop.name.toString()] = prop.value;
            return result;
        }

        juce::var positionToVar (const NodePosition& position)
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("x", exactFloatVar (position.x));
            obj->setProperty ("y", exactFloatVar (position.y));
            return juce::var (obj);
        }

        NodePosition positionFromVar (const juce::var& value)
        {
            NodePosition position;
            position.x = (float) value["x"];
            position.y = (float) value["y"];
            return position;
        }

        juce::var nodeToVar (const NodeInstance& node)
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("id", node.id);
            obj->setProperty ("type", node.type);
            obj->setProperty ("position", positionToVar (node.position));
            obj->setProperty ("parameters", parametersToVar (node.parameters));
            obj->setProperty ("properties", propertiesToVar (node.properties));
            if (! node.content.isVoid())
                obj->setProperty ("content", node.content);
            return juce::var (obj);
        }

        NodeInstance nodeFromVar (const juce::var& value)
        {
            NodeInstance node;
            node.id = value["id"].toString();
            node.type = value["type"].toString();
            node.position = positionFromVar (value["position"]);
            node.parameters = parametersFromVar (value["parameters"]);
            node.properties = propertiesFromVar (value["properties"]);
            node.content = value["content"].clone();
            return node;
        }

        juce::var connectionToVar (const Connection& connection)
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("fromNodeId", connection.fromNodeId);
            obj->setProperty ("fromPortId", connection.fromPortId);
            obj->setProperty ("toNodeId", connection.toNodeId);
            obj->setProperty ("toPortId", connection.toPortId);
            return juce::var (obj);
        }

        Connection connectionFromVar (const juce::var& value)
        {
            Connection connection;
            connection.fromNodeId = value["fromNodeId"].toString();
            connection.fromPortId = value["fromPortId"].toString();
            connection.toNodeId = value["toNodeId"].toString();
            connection.toPortId = value["toPortId"].toString();
            return connection;
        }

        juce::var viewToVar (const PatchViewState& view)
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("panX", exactFloatVar (view.panX));
            obj->setProperty ("panY", exactFloatVar (view.panY));
            obj->setProperty ("zoom", exactFloatVar (view.zoom));
            return juce::var (obj);
        }

        PatchViewState viewFromVar (const juce::var& value)
        {
            PatchViewState view;
            view.panX = (float) value["panX"];
            view.panY = (float) value["panY"];
            view.zoom = (float) value["zoom"];
            return view;
        }

        juce::var metaToVar (const PatchMeta& meta)
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("name", meta.name);
            obj->setProperty ("author", meta.author);
            // var has no native int64 — store as double. Millisecond
            // timestamps stay exactly representable in a double well past
            // any date this project will see (2^53 ms ~= year 287396).
            obj->setProperty ("createdAtMs", (double) meta.createdAtMs);
            obj->setProperty ("modifiedAtMs", (double) meta.modifiedAtMs);
            return juce::var (obj);
        }

        PatchMeta metaFromVar (const juce::var& value)
        {
            PatchMeta meta;
            meta.name = value["name"].toString();
            meta.author = value["author"].toString();
            meta.createdAtMs = (juce::int64) (double) value["createdAtMs"];
            meta.modifiedAtMs = (juce::int64) (double) value["modifiedAtMs"];
            return meta;
        }

        template <typename T, typename ToVarFn>
        juce::var listToVar (const std::vector<T>& items, ToVarFn toVar)
        {
            juce::Array<juce::var> array;
            for (const auto& item : items)
                array.add (toVar (item));
            return juce::var (array);
        }

        template <typename T, typename FromVarFn>
        std::vector<T> listFromVar (const juce::var& value, FromVarFn fromVar)
        {
            std::vector<T> result;
            if (auto* array = value.getArray())
                for (const auto& item : *array)
                    result.push_back (fromVar (item));
            return result;
        }

        PatchDocument documentFromVar (const juce::var& root)
        {
            PatchDocument doc;
            doc.schemaVersion = (int) root["schemaVersion"];
            doc.nodes = listFromVar<NodeInstance> (root["nodes"], nodeFromVar);
            doc.connections = listFromVar<Connection> (root["connections"], connectionFromVar);
            doc.outputNodeId = root["outputNodeId"].toString();
            doc.outputPortId = root["outputPortId"].toString();

            if (auto* array = root["macroValues"].getArray())
                for (const auto& v : *array)
                    doc.macroValues.push_back ((float) v);

            doc.view = viewFromVar (root["view"]);
            doc.meta = metaFromVar (root["meta"]);

            if (auto* assets = root["assets"].getDynamicObject())
                for (const auto& property : assets->getProperties())
                    doc.assets[property.name.toString()] = PatchAsset { property.value["type"].toString(), property.value["data"].toString() };
            return doc;
        }
    }

    juce::String serializePatchToJson (const PatchDocument& doc, bool prettyPrint)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("schemaVersion", doc.schemaVersion);
        obj->setProperty ("nodes", listToVar (doc.nodes, nodeToVar));
        obj->setProperty ("connections", listToVar (doc.connections, connectionToVar));
        obj->setProperty ("outputNodeId", doc.outputNodeId);
        obj->setProperty ("outputPortId", doc.outputPortId);

        juce::Array<juce::var> macroValuesArray;
        for (auto v : doc.macroValues)
            macroValuesArray.add (exactFloatVar (v));
        obj->setProperty ("macroValues", juce::var (macroValuesArray));

        obj->setProperty ("view", viewToVar (doc.view));
        obj->setProperty ("meta", metaToVar (doc.meta));

        auto* assets = new juce::DynamicObject();
        for (const auto& [id, asset] : doc.assets)
        {
            auto* entry = new juce::DynamicObject();
            entry->setProperty ("type", asset.mimeType);
            entry->setProperty ("data", asset.data);
            assets->setProperty (id, juce::var (entry));
        }
        obj->setProperty ("assets", juce::var (assets));

        return toJson (juce::var (obj), ! prettyPrint);
    }

    PatchParseResult parsePatchFromJson (const juce::String& json)
    {
        PatchParseResult result;

        juce::var root;
        const auto parseResult = juce::JSON::parse (json, root);

        if (parseResult.failed())
        {
            result.errorMessage = "JSON parse error: " + parseResult.getErrorMessage();
            return result;
        }

        if (! root.isObject())
        {
            result.errorMessage = "Patch root is not a JSON object";
            return result;
        }

        // No migrations (wiki/plans/Release.md R0.1): until the ID freeze, a
        // patch either is the current format or doesn't load.
        const auto fileVersion = (int) root["schemaVersion"];
        if (fileVersion != PatchDocument::currentSchemaVersion)
        {
            result.errorMessage = "This patch was made by a different version of Bazalt (format "
                                   + juce::String (fileVersion) + ", this build reads "
                                   + juce::String (PatchDocument::currentSchemaVersion) + ")";
            return result;
        }
        const auto& current = root;

        result.document = documentFromVar (current);
        result.success = true;
        return result;
    }
}
