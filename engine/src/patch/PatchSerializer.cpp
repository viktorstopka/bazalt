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
            obj->setProperty ("x", position.x);
            obj->setProperty ("y", position.y);
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
            obj->setProperty ("panX", view.panX);
            obj->setProperty ("panY", view.panY);
            obj->setProperty ("zoom", view.zoom);
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

        // v1 (M0-M6) addressed ports by integer index, not stable id
        // string (NodeGraph.h's Connection, PatchDocument.h's
        // outputPortIndex). The v1->v2 migration below resolves those
        // indices to ids using this table of the 8 node types that existed
        // at the time — the only source of truth available for old data,
        // since a v1 patch never recorded port ids at all. This is a
        // deliberate, accepted simplification (NODE_EDITOR.md §8/§12 item
        // 1): it only covers types that existed in schema v1. A node type
        // added after v2 needs no entry here (it never had an index-based
        // patch to migrate from); if v1 patches somehow needed to migrate
        // through a node type added later, thread a NodeFactory into
        // parsePatchFromJson instead of extending this table — don't grow
        // it speculatively.
        const std::unordered_map<juce::String, std::vector<juce::String>>& v1InputPortOrderByType()
        {
            static const std::unordered_map<juce::String, std::vector<juce::String>> table {
                { "osc.basic", {} },
                { "filter.svf", { "in" } },
                { "env.adsr", {} },
                { "amp.vca", { "audio", "gain" } },
                { "noise.burst", {} },
                { "mix.add2", { "a", "b" } },
                { "delay.basic", { "in" } },
                { "filter.onepole", { "in" } },
            };
            return table;
        }

        const std::unordered_map<juce::String, std::vector<juce::String>>& v1OutputPortOrderByType()
        {
            static const std::unordered_map<juce::String, std::vector<juce::String>> table {
                { "osc.basic", { "out" } },
                { "filter.svf", { "out" } },
                { "env.adsr", { "out" } },
                { "amp.vca", { "out" } },
                { "noise.burst", { "out" } },
                { "mix.add2", { "out" } },
                { "delay.basic", { "out" } },
                { "filter.onepole", { "out" } },
            };
            return table;
        }

        juce::String v1ResolvePortId (const std::unordered_map<juce::String, std::vector<juce::String>>& table,
                                       const juce::String& nodeType, int portIndex)
        {
            const auto it = table.find (nodeType);
            if (it == table.end() || portIndex < 0 || portIndex >= (int) it->second.size())
                return {}; // unknown v1 type or out-of-range index — caller surfaces this as a parse error
            return it->second[(size_t) portIndex];
        }

        juce::var migrateV1ToV2 (juce::var v1Root)
        {
            auto* root = new juce::DynamicObject();
            root->setProperty ("schemaVersion", 2);

            std::unordered_map<juce::String, juce::String> nodeTypeById;
            juce::Array<juce::var> nodesV2;

            if (auto* nodesArray = v1Root["nodes"].getArray())
            {
                for (const auto& nodeV1 : *nodesArray)
                {
                    const auto id = nodeV1["id"].toString();
                    const auto type = nodeV1["type"].toString();
                    nodeTypeById[id] = type;

                    auto* nodeV2 = new juce::DynamicObject();
                    nodeV2->setProperty ("id", id);
                    nodeV2->setProperty ("type", type);
                    nodeV2->setProperty ("position", positionToVar ({}));
                    nodeV2->setProperty ("parameters", nodeV1["parameters"]);
                    nodeV2->setProperty ("properties", propertiesToVar ({}));
                    nodesV2.add (juce::var (nodeV2));
                }
            }

            root->setProperty ("nodes", juce::var (nodesV2));

            juce::Array<juce::var> connectionsV2;
            if (auto* connectionsArray = v1Root["connections"].getArray())
            {
                for (const auto& connV1 : *connectionsArray)
                {
                    const auto fromNodeId = connV1["fromNodeId"].toString();
                    const auto toNodeId = connV1["toNodeId"].toString();

                    auto* connV2 = new juce::DynamicObject();
                    connV2->setProperty ("fromNodeId", fromNodeId);
                    connV2->setProperty ("fromPortId",
                        v1ResolvePortId (v1OutputPortOrderByType(), nodeTypeById[fromNodeId], (int) connV1["fromPortIndex"]));
                    connV2->setProperty ("toNodeId", toNodeId);
                    connV2->setProperty ("toPortId",
                        v1ResolvePortId (v1InputPortOrderByType(), nodeTypeById[toNodeId], (int) connV1["toPortIndex"]));
                    connectionsV2.add (juce::var (connV2));
                }
            }

            root->setProperty ("connections", juce::var (connectionsV2));
            root->setProperty ("outputNodeId", v1Root["outputNodeId"]);
            root->setProperty ("outputPortId",
                v1ResolvePortId (v1OutputPortOrderByType(), nodeTypeById[v1Root["outputNodeId"].toString()],
                                  (int) v1Root["outputPortIndex"]));
            root->setProperty ("macroMappings", v1Root["macroMappings"]);
            root->setProperty ("macroValues", v1Root["macroValues"]);
            root->setProperty ("view", viewToVar ({})); // v1 had no view state — default pan/zoom (zoom = 1)
            root->setProperty ("meta", v1Root["meta"]);

            return juce::var (root);
        }

        // vN -> vN+1 migrations, keyed by the version they migrate FROM.
        using Migration = std::function<juce::var (juce::var)>;

        const std::unordered_map<int, Migration>& getMigrations()
        {
            static const std::unordered_map<int, Migration> migrations {
                { 1, migrateV1ToV2 },
            };
            return migrations;
        }

        PatchDocument documentFromVar (const juce::var& root)
        {
            PatchDocument doc;
            doc.schemaVersion = (int) root["schemaVersion"];
            doc.nodes = listFromVar<NodeInstance> (root["nodes"], nodeFromVar);
            doc.connections = listFromVar<Connection> (root["connections"], connectionFromVar);
            doc.outputNodeId = root["outputNodeId"].toString();
            doc.outputPortId = root["outputPortId"].toString();
            doc.macroMappings = listFromVar<MacroMapping> (root["macroMappings"], macroMappingFromVar);

            if (auto* array = root["macroValues"].getArray())
                for (const auto& v : *array)
                    doc.macroValues.push_back ((float) v);

            doc.view = viewFromVar (root["view"]);
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
        obj->setProperty ("outputPortId", doc.outputPortId);
        obj->setProperty ("macroMappings", listToVar (doc.macroMappings, macroMappingToVar));

        juce::Array<juce::var> macroValuesArray;
        for (auto v : doc.macroValues)
            macroValuesArray.add (v);
        obj->setProperty ("macroValues", juce::var (macroValuesArray));

        obj->setProperty ("view", viewToVar (doc.view));
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
