#include "bazalt/engine/patch/PatchSerializer.h"
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
                { "osc.analog", {} },
                { "filter.svf", { "in" } },
                { "env.adsr", {} },
                { "mix.gain", { "audio", "gain" } },
                { "excite.burst", {} },
                { "mix.sum", { "a", "b" } },
                { "delay.line", { "in" } },
                { "filter.onepole", { "in" } },
            };
            return table;
        }

        const std::unordered_map<juce::String, std::vector<juce::String>>& v1OutputPortOrderByType()
        {
            static const std::unordered_map<juce::String, std::vector<juce::String>> table {
                { "osc.analog", { "out" } },
                { "filter.svf", { "out" } },
                { "env.adsr", { "out" } },
                { "mix.gain", { "out" } },
                { "excite.burst", { "out" } },
                { "mix.sum", { "out" } },
                { "delay.line", { "out" } },
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
            root->setProperty ("macroValues", v1Root["macroValues"]);
            root->setProperty ("view", viewToVar ({})); // v1 had no view state — default pan/zoom (zoom = 1)
            root->setProperty ("meta", v1Root["meta"]);

            return juce::var (root);
        }

        // v2 -> v3 (M21): math.add / math.multiply / mix.sum became growable
        // port groups, so their fixed `a`/`b` inputs are now `in.0`/`in.1`
        // (PortGroups.h). Only the two port IDs on connections INTO nodes of
        // those three types change; nothing else in a v2 document mentions
        // them (none of the three has a parameter or a macro-mappable
        // value), and outputs were and stay `out`.
        juce::var migrateV2ToV3 (juce::var v2Root)
        {
            auto root = v2Root.clone(); // deep copy — the caller's document is left untouched

            std::unordered_map<juce::String, juce::String> nodeTypeById;
            if (auto* nodes = root["nodes"].getArray())
                for (const auto& node : *nodes)
                    nodeTypeById[node["id"].toString()] = node["type"].toString();

            static const std::unordered_set<juce::String> growableTypes { "math.add", "math.multiply", "mix.sum" };

            if (auto* connections = root["connections"].getArray())
            {
                for (auto& connection : *connections)
                {
                    const auto typeIt = nodeTypeById.find (connection["toNodeId"].toString());
                    if (typeIt == nodeTypeById.end() || growableTypes.find (typeIt->second) == growableTypes.end())
                        continue;

                    const auto portId = connection["toPortId"].toString();
                    if (portId == "a")
                        connection.getDynamicObject()->setProperty ("toPortId", "in.0");
                    else if (portId == "b")
                        connection.getDynamicObject()->setProperty ("toPortId", "in.1");
                }
            }

            root.getDynamicObject()->setProperty ("schemaVersion", 3);
            return root;
        }

        // v3 -> v4 (wiki/NODES_Gaps.md's `redundant-composable-param`
        // finding): mix.sum's level.N port is removed — it duplicated what
        // a mix.gain node placed in front of an input already does. Every
        // level.N a v3 document ever touched (a non-default stored
        // parameter, or a real connection feeding it) becomes a real,
        // visible mix.gain node spliced between that in.N's original
        // source and mix.sum's own in.N, so an old patch keeps sounding
        // the same — never silently dropped. A level.N left at its default
        // (1.0, unconnected) needs nothing: a plain in.N connection with no
        // gain node in front of it already behaves identically.
        juce::var migrateV3ToV4 (juce::var v3Root)
        {
            auto root = v3Root.clone(); // deep copy — the caller's document is left untouched

            auto* nodesArray = root["nodes"].getArray();
            auto* connectionsArray = root["connections"].getArray();

            if (nodesArray != nullptr && connectionsArray != nullptr)
            {
                std::unordered_set<juce::String> existingNodeIds;
                for (const auto& node : *nodesArray)
                    existingNodeIds.insert (node["id"].toString());

                auto uniqueGainNodeId = [&existingNodeIds] (const juce::String& base)
                {
                    auto candidate = base;
                    for (int i = 2; existingNodeIds.find (candidate) != existingNodeIds.end(); ++i)
                        candidate = base + juce::String (i);
                    existingNodeIds.insert (candidate);
                    return candidate;
                };

                // Index-based, over a count captured up front: the loop body
                // below appends new mix.gain nodes to *nodesArray* itself,
                // which can reallocate its backing storage — a live
                // reference into the array is never held across one of
                // those appends. Everything this loop needs from a mix.sum
                // node is copied into a local (or, for `parameters`, a
                // DynamicObject* — heap-stable independent of where the
                // array's own backing buffer lives) before any mutation.
                const auto originalNodeCount = nodesArray->size();
                for (int nodeIndex = 0; nodeIndex < originalNodeCount; ++nodeIndex)
                {
                    const auto& node = nodesArray->getReference (nodeIndex);
                    if (node["type"].toString() != "mix.sum")
                        continue;

                    const juce::String mixSumId = node["id"].toString();
                    auto* paramsObj = node["parameters"].getDynamicObject();
                    const auto posX = (float) node["position"]["x"];
                    const auto posY = (float) node["position"]["y"];
                    if (paramsObj == nullptr)
                        continue;

                    // Every level.N this node ever touched — a level.N can
                    // exist ONLY as a connection target, with no stored
                    // parameter at all (a user who wired a modulator onto it
                    // without ever first dragging its slider), so this has
                    // to look at both sources, not parameters alone; a
                    // level.N seen in both is deduplicated via the set.
                    std::vector<juce::String> levelPortIds;
                    {
                        std::unordered_set<juce::String> seen;
                        for (const auto& prop : paramsObj->getProperties())
                            if (const auto key = prop.name.toString(); key.startsWith ("level.") && seen.insert (key).second)
                                levelPortIds.push_back (key);
                        for (const auto& c : *connectionsArray)
                            if (c["toNodeId"].toString() == mixSumId)
                                if (const auto key = c["toPortId"].toString(); key.startsWith ("level.") && seen.insert (key).second)
                                    levelPortIds.push_back (key);
                    }

                    for (const auto& levelPortId : levelPortIds)
                    {
                        // A stored parameter value if this level.N ever had
                        // one; the port's own declared default (1.0,
                        // GainNode.h) otherwise — a level.N that only ever
                        // existed as a connection target has no parameter to
                        // read here.
                        const auto levelValue = paramsObj->hasProperty (juce::Identifier (levelPortId))
                                                   ? (float) paramsObj->getProperty (juce::Identifier (levelPortId))
                                                   : 1.0f;
                        paramsObj->removeProperty (juce::Identifier (levelPortId));

                        const auto index = levelPortId.fromFirstOccurrenceOf ("level.", false, false);
                        const auto inPortId = "in." + index;

                        // Was level.N itself fed by a connection (modulated),
                        // rather than only a constant slider value? Find and
                        // remove it — its port is gone, there's nowhere left
                        // for that connection to target.
                        juce::var levelFromNodeId, levelFromPortId;
                        auto hasLevelConnection = false;
                        for (int i = connectionsArray->size(); --i >= 0;)
                        {
                            const auto& c = connectionsArray->getReference (i);
                            if (c["toNodeId"].toString() == mixSumId && c["toPortId"].toString() == levelPortId)
                            {
                                levelFromNodeId = c["fromNodeId"];
                                levelFromPortId = c["fromPortId"];
                                hasLevelConnection = true;
                                connectionsArray->remove (i);
                                break; // one source per input — the invariant this whole codebase enforces
                            }
                        }

                        if (! hasLevelConnection && levelValue == 1.0f)
                            continue; // default, unconnected: nothing to preserve

                        const auto gainNodeId = uniqueGainNodeId (mixSumId + "_gain" + index);

                        auto* gainNode = new juce::DynamicObject();
                        gainNode->setProperty ("id", gainNodeId);
                        gainNode->setProperty ("type", "mix.gain");
                        auto* positionObj = new juce::DynamicObject();
                        positionObj->setProperty ("x", posX - 160.0f);
                        positionObj->setProperty ("y", posY + 60.0f * (float) index.getIntValue());
                        gainNode->setProperty ("position", juce::var (positionObj));
                        auto* gainParams = new juce::DynamicObject();
                        if (! hasLevelConnection)
                            gainParams->setProperty ("gain", levelValue); // the constant the slider held
                        gainNode->setProperty ("parameters", juce::var (gainParams));
                        gainNode->setProperty ("properties", juce::var (new juce::DynamicObject()));
                        nodesArray->add (juce::var (gainNode));

                        // Retarget in.N's original source (if any) into the
                        // new gain node's "audio" input instead of mix.sum
                        // directly — a plain in-place property edit, safe to
                        // do with a range-based loop since nothing is added
                        // to or removed from connectionsArray in this pass.
                        for (auto& connection : *connectionsArray)
                        {
                            if (connection["toNodeId"].toString() == mixSumId && connection["toPortId"].toString() == inPortId)
                            {
                                connection.getDynamicObject()->setProperty ("toNodeId", gainNodeId);
                                connection.getDynamicObject()->setProperty ("toPortId", "audio");
                                break;
                            }
                        }
                        // (no match: in.N simply had nothing feeding it yet — the
                        // new gain node inherits that same silence, correctly)

                        if (hasLevelConnection)
                        {
                            auto* modConnection = new juce::DynamicObject();
                            modConnection->setProperty ("fromNodeId", levelFromNodeId);
                            modConnection->setProperty ("fromPortId", levelFromPortId);
                            modConnection->setProperty ("toNodeId", gainNodeId);
                            modConnection->setProperty ("toPortId", "gain");
                            connectionsArray->add (juce::var (modConnection));
                        }

                        auto* outConnection = new juce::DynamicObject();
                        outConnection->setProperty ("fromNodeId", gainNodeId);
                        outConnection->setProperty ("fromPortId", "out");
                        outConnection->setProperty ("toNodeId", mixSumId);
                        outConnection->setProperty ("toPortId", inPortId);
                        connectionsArray->add (juce::var (outConnection));
                    }
                }
            }

            root.getDynamicObject()->setProperty ("schemaVersion", 4);
            return root;
        }

        // Schema v5 (real stereo cable redesign, PatchDocument.h's own
        // comment on `currentSchemaVersion` has the full reasoning):
        // space.pan/space.width/io.output/mix.downmix/stereo.split/
        // stereo.combine collapsed their left/right port pairs into real
        // Channels::Stereo ports. No node/connection rewriting here on
        // purpose — CLAUDE.md rule 3 is suspended for now, and nothing real
        // depends on the pre-v5 port ids. This is a version-number bump
        // ONLY, kept as a real migration entry (not just a version check
        // skipped elsewhere) so an old v1-v4 patch still PARSES successfully
        // through the existing migration chain, exactly as before this
        // redesign — a patch that happens to reference one of the six
        // renamed nodes' old port ids fails later, at GraphCompiler::compile()
        // time, with a clear "no such port" error naming the exact node -
        // not silently misinterpreted, and not an opaque parse failure for
        // every old patch regardless of whether it used a stereo node at all.
        juce::var migrateV4ToV5 (juce::var v4Root)
        {
            auto root = v4Root.clone();
            root.getDynamicObject()->setProperty ("schemaVersion", 5);
            return root;
        }

        // wiki/plans/DomainRedesign.md Batch 1b: "instance.mix" renamed to
        // "instance.sum" — same reasoning as migrateV4ToV5 above (CLAUDE.md
        // rule 3 is suspended, so this is a free rename with no id-rewriting
        // needed): a version-number bump ONLY, kept as a real migration
        // entry so an old v1-v5 patch (one that used the old type id) still
        // PARSES successfully through the existing migration chain. It just
        // won't compile once loaded — GraphCompiler rejects the now-unknown
        // "instance.mix" type id with a clear "Unknown node type" error,
        // not silently misinterpreted and not an opaque parse failure.
        juce::var migrateV5ToV6 (juce::var v5Root)
        {
            auto root = v5Root.clone();
            root.getDynamicObject()->setProperty ("schemaVersion", 6);
            return root;
        }

        // wiki/plans/UtilMacro.md, archive_docs/decisions/
        // 0030-util-macro-is-a-real-wireable-node.md: `macroMappings` is
        // dropped from PatchDocument entirely — a real util.macro node now
        // claims its own slot via an ordinary structural parameter and is
        // wired in like any other node, so the mapping table is derived
        // fresh from the graph every compile (GraphEditController.cpp),
        // never persisted. A version-number bump ONLY, same shape as
        // migrateV4ToV5/migrateV5ToV6: a v6 document's own "macroMappings"
        // property (never node-derived in the first place, and — per
        // PluginProcessor's own setDefaultMacroMappings(), removed in the
        // same change — dead even in the one shipping build that still
        // wrote it) is simply not carried forward; documentFromVar() below
        // no longer reads that key at all, so its presence or absence in
        // the incoming JSON is equally harmless.
        juce::var migrateV6ToV7 (juce::var v6Root)
        {
            auto root = v6Root.clone();
            root.getDynamicObject()->setProperty ("schemaVersion", 7);
            return root;
        }

        // 2026-10-04, design/Map.png: `adapt.remap` became `adapt.map` and the
        // old two-parameter `adapt.map` stopped existing (MapNode.h). Unlike
        // the bump-only migrations above, this one rewrites ids, because the
        // user's own saved patches (patches/CatPurr.json among them) use
        // both nodes:
        //   - old adapt.map {min, max} -> new adapt.map with inMin/inMax 0..1
        //     and outMin/outMax = min/max. The old node also rescaled a
        //     Bipolar source from -1..1; a migrated one fed by a Bipolar
        //     source needs its In Min edited to -1 by hand (the polarity of
        //     the source isn't knowable from the document alone).
        //   - adapt.remap -> adapt.map, every "adapt.remap.*" parameter and
        //     connection port id renamed to "adapt.map.*".
        // Old adapt.map nodes are rewritten FIRST, so a renamed remap is never
        // mistaken for one.
        juce::var migrateV7ToV8 (juce::var v7Root)
        {
            auto root = v7Root.clone();

            auto renameKeys = [] (juce::DynamicObject& object, const juce::String& from, const juce::String& to)
            {
                juce::NamedValueSet renamed;
                for (const auto& property : object.getProperties())
                {
                    const auto name = property.name.toString();
                    renamed.set (name.startsWith (from) ? to + name.substring (from.length()) : name, property.value);
                }
                object.clear();
                for (const auto& property : renamed)
                    object.setProperty (property.name, property.value);
            };

            if (auto* nodes = root["nodes"].getArray())
            {
                for (auto& node : *nodes)
                {
                    auto* object = node.getDynamicObject();
                    if (object == nullptr)
                        continue;

                    const auto type = object->getProperty ("type").toString();
                    auto* parameters = object->getProperty ("parameters").getDynamicObject();

                    if (type == "adapt.map" && parameters != nullptr)
                    {
                        const auto min = parameters->hasProperty ("adapt.map.min") ? parameters->getProperty ("adapt.map.min") : juce::var (0.0f);
                        const auto max = parameters->hasProperty ("adapt.map.max") ? parameters->getProperty ("adapt.map.max") : juce::var (1.0f);
                        parameters->removeProperty ("adapt.map.min");
                        parameters->removeProperty ("adapt.map.max");
                        parameters->setProperty ("adapt.map.inMin", 0.0f);
                        parameters->setProperty ("adapt.map.inMax", 1.0f);
                        parameters->setProperty ("adapt.map.outMin", min);
                        parameters->setProperty ("adapt.map.outMax", max);
                    }
                    else if (type == "adapt.remap")
                    {
                        object->setProperty ("type", "adapt.map");
                        if (parameters != nullptr)
                            renameKeys (*parameters, "adapt.remap.", "adapt.map.");
                    }
                }
            }

            if (auto* connections = root["connections"].getArray())
                for (auto& connection : *connections)
                    if (auto* object = connection.getDynamicObject())
                        for (const auto* key : { "fromPortId", "toPortId" })
                        {
                            const auto portId = object->getProperty (key).toString();
                            if (portId.startsWith ("adapt.remap."))
                                object->setProperty (key, "adapt.map." + portId.fromFirstOccurrenceOf ("adapt.remap.", false, false));
                        }

            root.getDynamicObject()->setProperty ("schemaVersion", 8);
            return root;
        }

        // 2026-10-04: view.scope and view.glance were removed outright (the
        // phase-locked view.cycle replaces both). A view.scope was a dead end,
        // so it and its connections simply go. A view.glance was a pass-
        // through, so it is spliced OUT: whatever fed it now feeds everything
        // it fed (and the graph output, if it was that), so the patch still
        // sounds the same.
        juce::var migrateV8ToV9 (juce::var v8Root)
        {
            auto root = v8Root.clone();
            auto* nodes = root["nodes"].getArray();
            auto* connections = root["connections"].getArray();

            juce::StringArray removed, glances;
            if (nodes != nullptr)
            {
                for (int i = nodes->size(); --i >= 0;)
                {
                    const auto type = (*nodes)[i]["type"].toString();
                    if (type == "view.scope" || type == "view.glance")
                    {
                        removed.add ((*nodes)[i]["id"].toString());
                        if (type == "view.glance")
                            glances.add ((*nodes)[i]["id"].toString());
                        nodes->remove (i);
                    }
                }
            }

            if (connections != nullptr && ! removed.isEmpty())
            {
                for (const auto& glance : glances)
                {
                    juce::var feeder;
                    for (const auto& c : *connections)
                        if (c["toNodeId"].toString() == glance)
                            feeder = c;
                    if (feeder.isVoid())
                        continue;

                    juce::Array<juce::var> bridged;
                    for (const auto& c : *connections)
                    {
                        if (c["fromNodeId"].toString() != glance)
                            continue;
                        auto* bridge = new juce::DynamicObject();
                        bridge->setProperty ("fromNodeId", feeder["fromNodeId"]);
                        bridge->setProperty ("fromPortId", feeder["fromPortId"]);
                        bridge->setProperty ("toNodeId", c["toNodeId"]);
                        bridge->setProperty ("toPortId", c["toPortId"]);
                        bridged.add (juce::var (bridge));
                    }
                    connections->addArray (bridged);

                    if (root["outputNodeId"].toString() == glance)
                    {
                        root.getDynamicObject()->setProperty ("outputNodeId", feeder["fromNodeId"]);
                        root.getDynamicObject()->setProperty ("outputPortId", feeder["fromPortId"]);
                    }
                }

                for (int i = connections->size(); --i >= 0;)
                    if (removed.contains ((*connections)[i]["fromNodeId"].toString()) || removed.contains ((*connections)[i]["toNodeId"].toString()))
                        connections->remove (i);
            }

            root.getDynamicObject()->setProperty ("schemaVersion", 9);
            return root;
        }

        // 2026-10-04: logic.boolean (one node, an Op menu AND/OR/XOR/NAND/NOR)
        // became separate logic.and / logic.or / logic.xor nodes with an
        // Invert switch (LogicGateNodes.h). Ports (in.N, out) are unchanged.
        juce::var migrateV9ToV10 (juce::var v9Root)
        {
            auto root = v9Root.clone();
            if (auto* nodes = root["nodes"].getArray())
            {
                for (auto& node : *nodes)
                {
                    auto* object = node.getDynamicObject();
                    if (object == nullptr || object->getProperty ("type").toString() != "logic.boolean")
                        continue;

                    auto* parameters = object->getProperty ("parameters").getDynamicObject();
                    const auto op = parameters != nullptr && parameters->hasProperty ("logic.boolean.op")
                                        ? juce::roundToInt ((float) parameters->getProperty ("logic.boolean.op")) : 0;
                    // 0 AND, 1 OR, 2 XOR, 3 NAND, 4 NOR
                    const juce::String type = (op == 1 || op == 4) ? "logic.or" : op == 2 ? "logic.xor" : "logic.and";
                    object->setProperty ("type", type);

                    auto* fresh = new juce::DynamicObject();
                    if (op >= 3)
                        fresh->setProperty (type + ".invert", 1.0f);
                    object->setProperty ("parameters", juce::var (fresh));
                }
            }
            root.getDynamicObject()->setProperty ("schemaVersion", 10);
            return root;
        }

        // vN -> vN+1 migrations, keyed by the version they migrate FROM.
        using Migration = std::function<juce::var (juce::var)>;

        const std::unordered_map<int, Migration>& getMigrations()
        {
            static const std::unordered_map<int, Migration> migrations {
                { 1, migrateV1ToV2 },
                { 2, migrateV2ToV3 },
                { 3, migrateV3ToV4 },
                { 4, migrateV4ToV5 },
                { 5, migrateV5ToV6 },
                { 6, migrateV6ToV7 },
                { 7, migrateV7ToV8 },
                { 8, migrateV8ToV9 },
                { 9, migrateV9ToV10 },
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
