#include "bazalt/engine/patch/PatchSerializer.h"
#include <functional>
#include <unordered_map>

namespace bazalt::engine
{
    namespace
    {
        juce::var parametersToVar (const std::unordered_map<juce::String, float>& parameters)
        {
            auto* obj = new juce::DynamicObject();
            for (const auto& [key, value] : parameters)
                obj->setProperty (juce::Identifier (key), value);
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

        juce::var nodeToVar (const NodeInstance& node)
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("id", node.id);
            obj->setProperty ("type", node.type);
            obj->setProperty ("parameters", parametersToVar (node.parameters));
            return juce::var (obj);
        }

        NodeInstance nodeFromVar (const juce::var& value)
        {
            NodeInstance node;
            node.id = value["id"].toString();
            node.type = value["type"].toString();
            node.parameters = parametersFromVar (value["parameters"]);
            return node;
        }

        juce::var connectionToVar (const Connection& connection)
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("fromNodeId", connection.fromNodeId);
            obj->setProperty ("fromPortIndex", connection.fromPortIndex);
            obj->setProperty ("toNodeId", connection.toNodeId);
            obj->setProperty ("toPortIndex", connection.toPortIndex);
            return juce::var (obj);
        }

        Connection connectionFromVar (const juce::var& value)
        {
            Connection connection;
            connection.fromNodeId = value["fromNodeId"].toString();
            connection.fromPortIndex = (int) value["fromPortIndex"];
            connection.toNodeId = value["toNodeId"].toString();
            connection.toPortIndex = (int) value["toPortIndex"];
            return connection;
        }

        juce::var macroMappingToVar (const MacroMapping& mapping)
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("macroIndex", mapping.macroIndex);
            obj->setProperty ("targetNodeId", mapping.targetNodeId);
            obj->setProperty ("targetParameterId", mapping.targetParameterId);
            obj->setProperty ("rangeMin", mapping.rangeMin);
            obj->setProperty ("rangeMax", mapping.rangeMax);
            return juce::var (obj);
        }

        MacroMapping macroMappingFromVar (const juce::var& value)
        {
            MacroMapping mapping;
            mapping.macroIndex = (int) value["macroIndex"];
            mapping.targetNodeId = value["targetNodeId"].toString();
            mapping.targetParameterId = value["targetParameterId"].toString();
            mapping.rangeMin = (float) value["rangeMin"];
            mapping.rangeMax = (float) value["rangeMax"];
            return mapping;
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

        // vN -> vN+1 migrations, keyed by the version they migrate FROM.
        // Empty today (currentSchemaVersion == 1) — this map is the
        // mechanism, not evidence anything needs migrating yet. Add an
        // entry here the day schemaVersion becomes 2, never retrofit.
        using Migration = std::function<juce::var (juce::var)>;

        const std::unordered_map<int, Migration>& getMigrations()
        {
            static const std::unordered_map<int, Migration> migrations;
            return migrations;
        }

        PatchDocument documentFromVar (const juce::var& root)
        {
            PatchDocument doc;
            doc.schemaVersion = (int) root["schemaVersion"];
            doc.nodes = listFromVar<NodeInstance> (root["nodes"], nodeFromVar);
            doc.connections = listFromVar<Connection> (root["connections"], connectionFromVar);
            doc.outputNodeId = root["outputNodeId"].toString();
            doc.outputPortIndex = (int) root["outputPortIndex"];
            doc.macroMappings = listFromVar<MacroMapping> (root["macroMappings"], macroMappingFromVar);

            if (auto* array = root["macroValues"].getArray())
                for (const auto& v : *array)
                    doc.macroValues.push_back ((float) v);

            doc.meta = metaFromVar (root["meta"]);
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
        obj->setProperty ("outputPortIndex", doc.outputPortIndex);
        obj->setProperty ("macroMappings", listToVar (doc.macroMappings, macroMappingToVar));

        juce::Array<juce::var> macroValuesArray;
        for (auto v : doc.macroValues)
            macroValuesArray.add (v);
        obj->setProperty ("macroValues", juce::var (macroValuesArray));

        obj->setProperty ("meta", metaToVar (doc.meta));

        return juce::JSON::toString (juce::var (obj), ! prettyPrint);
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

        auto fileVersion = (int) root["schemaVersion"];
        auto current = root;
        const auto& migrations = getMigrations();

        while (fileVersion < PatchDocument::currentSchemaVersion)
        {
            const auto it = migrations.find (fileVersion);
            if (it == migrations.end())
            {
                result.errorMessage = "No migration registered from schema version " + juce::String (fileVersion);
                return result;
            }

            current = it->second (current);
            fileVersion = (int) current["schemaVersion"];
        }

        if (fileVersion > PatchDocument::currentSchemaVersion)
        {
            result.errorMessage = "Patch schema version " + juce::String (fileVersion)
                                   + " is newer than this build supports ("
                                   + juce::String (PatchDocument::currentSchemaVersion) + ")";
            return result;
        }

        result.document = documentFromVar (current);
        result.success = true;
        return result;
    }
}
