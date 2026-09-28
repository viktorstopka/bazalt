#include "bazalt/engine/graph/DomainSplitter.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bazalt::engine
{
    namespace
    {
        constexpr const char* instanceMixTypeId = "instance.mix";
        constexpr const char* instanceMixInputPortId = "in";
        constexpr const char* instanceAllocatorTypeId = "instance.allocator";

        std::unordered_set<juce::String> reachableFollowing (const juce::String& start,
                                                               const std::unordered_map<juce::String, std::vector<juce::String>>& edges,
                                                               bool includeStart)
        {
            std::unordered_set<juce::String> visited;
            std::vector<juce::String> stack { start };

            if (includeStart)
                visited.insert (start);

            while (! stack.empty())
            {
                const auto current = stack.back();
                stack.pop_back();

                const auto it = edges.find (current);
                if (it == edges.end())
                    continue;

                for (const auto& next : it->second)
                    if (visited.insert (next).second)
                        stack.push_back (next);
            }

            return visited;
        }
    }

    DomainSplitResult DomainSplitter::split (const NodeGraph& graph)
    {
        DomainSplitResult result;
        const auto& nodes = graph.getNodes();
        const auto& connections = graph.getConnections();

        juce::String instanceMixId;
        juce::String instanceAllocatorId;
        int instanceMixCount = 0;
        int instanceAllocatorCount = 0;
        for (const auto& node : nodes)
        {
            if (node.type == instanceMixTypeId)
            {
                instanceMixId = node.id;
                ++instanceMixCount;
            }
            else if (node.type == instanceAllocatorTypeId)
            {
                instanceAllocatorId = node.id;
                ++instanceAllocatorCount;
            }
        }

        if (instanceMixCount == 0)
        {
            if (instanceAllocatorCount == 0)
            {
                result.success = true;
                result.hasGlobalDomain = false;
                result.monoOnly = true;
                result.voiceGraph = graph;
                return result;
            }

            // 09-28-InstanceAllocator.1: an instance.allocator that genuinely
            // exists must ALWAYS receive MIDI and run its own per-voice
            // plans — dispatch (PluginProcessor::handleMidiEvent) and
            // rendering are keyed off `monoOnly` alone, so a real allocator
            // must never leave it true, regardless of whether its output
            // currently reaches the graph's designated output. (First cut of
            // this fix got this wrong: it set monoOnly=true whenever the
            // output wasn't allocator-reachable, which also silently
            // disabled MIDI dispatch entirely — correct for an UNRELATED,
            // disconnected allocator, wrong for one the user is actively
            // wiring/testing, e.g. via a view.glance tap on its gate output
            // before it's wired anywhere else. Found live, same session.)
            result.monoOnly = false;

            std::unordered_map<juce::String, std::vector<juce::String>> successorsOf, predecessorsOf;
            for (const auto& connection : connections)
            {
                successorsOf[connection.fromNodeId].push_back (connection.toNodeId);
                predecessorsOf[connection.toNodeId].push_back (connection.fromNodeId);
            }

            // Forward (what the allocator feeds) UNION backward (what feeds
            // the allocator) — a real bug found live via the project's own
            // simplest proof graph (buildVoiceProofGraph(): "noteIn ->
            // allocator.spawn" is exactly this shape): io.noteIn isn't
            // "inside" the voice domain in the sense of running per-voice
            // DSP, but it's also not unrelated global content either — it's
            // the allocator's own trigger source, exactly the "mono source
            // feeding the poly region" DOMAINS.md §2 already says is free.
            // Forward-only reachability treated it as a domain crossing.
            auto voiceReachable = reachableFollowing (instanceAllocatorId, successorsOf, true);
            for (const auto& id : reachableFollowing (instanceAllocatorId, predecessorsOf, false))
                voiceReachable.insert (id);

            if (voiceReachable.count (graph.getOutputNodeId()) > 0)
            {
                // The designated output genuinely lives in the voice domain
                // — the ordinary "no instance.mix needed, voice sum IS the
                // final output" case that already worked before this
                // milestone. voiceGraph is the whole graph, exactly as
                // every such graph already compiles today.
                result.success = true;
                result.hasGlobalDomain = false;
                result.voiceGraph = graph;
                return result;
            }

            // The designated output is NOT reachable from the allocator:
            // split for real. voiceGraph becomes ONLY what's actually
            // allocator-reachable (so voices run and are observable via
            // taps/previews, contributing nothing to the audible signal
            // yet); everything else becomes an independent, unbridged
            // global region that runs unconditionally every block — see
            // hasGlobalDomain's own doc comment for how PluginProcessor
            // tells this apart from the bridged-via-instance.mix case
            // (instanceMixNodeId stays empty here).
            for (const auto& connection : connections)
            {
                const auto fromInVoice = voiceReachable.count (connection.fromNodeId) > 0;
                const auto toInVoice = voiceReachable.count (connection.toNodeId) > 0;

                if (fromInVoice != toInVoice)
                {
                    result.errorMessage = "Node '" + connection.fromNodeId + "' ("
                                           + (fromInVoice ? juce::String ("voice domain") : juce::String ("global domain"))
                                           + ") feeds node '" + connection.toNodeId + "' ("
                                           + (toInVoice ? juce::String ("voice domain") : juce::String ("global domain"))
                                           + ") directly: with no instance.mix node present, a signal can only cross"
                                           + " between the voice-reachable region and the rest of the graph through one.";
                    return result;
                }
            }

            NodeGraph voiceOnly;
            for (const auto& node : nodes)
                if (voiceReachable.count (node.id) > 0)
                    voiceOnly.addNode (node);
            for (const auto& connection : connections)
                if (voiceReachable.count (connection.fromNodeId) > 0)
                    voiceOnly.addConnection (connection);
            // voiceOnly's own output designation is never read by
            // PluginProcessor in this branch (the independent global
            // region's output is what's final) — GraphCompiler still needs
            // SOME real, always-present output port to compile against, so
            // point it at the allocator's own primary output.
            voiceOnly.setOutput (instanceAllocatorId, "gate");

            NodeGraph independentGlobal;
            for (const auto& node : nodes)
                if (voiceReachable.count (node.id) == 0)
                    independentGlobal.addNode (node);
            for (const auto& connection : connections)
                if (voiceReachable.count (connection.fromNodeId) == 0)
                    independentGlobal.addConnection (connection);
            independentGlobal.setOutput (graph.getOutputNodeId(), graph.getOutputPortId());

            result.success = true;
            result.hasGlobalDomain = true; // instanceMixNodeId stays empty: unbridged, see its own comment
            result.voiceGraph = std::move (voiceOnly);
            result.globalGraph = std::move (independentGlobal);
            return result;
        }

        if (instanceMixCount > 1)
        {
            result.errorMessage = "Only one instance.mix node is supported per graph (found "
                                   + juce::String (instanceMixCount)
                                   + ") — placing several is a future capability, "
                                   + "not yet built (ExecutionPlan needs multi-output support first)";
            return result;
        }

        if (instanceAllocatorCount > 1)
        {
            result.errorMessage = "Only one instance.allocator node is supported per graph (found "
                                   + juce::String (instanceAllocatorCount)
                                   + ") — multiple simultaneous instanced regions are M28 (Swarm) territory, not built yet";
            return result;
        }

        const Connection* incoming = nullptr;
        int incomingCount = 0;
        for (const auto& connection : connections)
        {
            if (connection.toNodeId == instanceMixId && connection.toPortId == instanceMixInputPortId)
            {
                incoming = &connection;
                ++incomingCount;
            }
        }

        // A freshly-placed instance.mix has no connection yet — that must
        // be placeable on its own (the UI places a node, then wires it, as
        // two separate commands; requiring the wire to already exist would
        // make it impossible to ever place one at all). Treat this exactly
        // like "no instance.mix node present" (the domain boundary simply
        // isn't active yet) rather than rejecting the whole graph — a real
        // gap found via actual hands-on testing (M19), not caught by any
        // test, since every existing test builds its graph in one shot via
        // setGraph() rather than incrementally via addNode()+connect().
        // Only a genuine double-connection (which the UI itself never
        // produces, but a hand-edited or scripted patch could) stays an
        // error.
        if (incomingCount == 0)
        {
            result.success = true;
            result.hasGlobalDomain = false;
            result.monoOnly = instanceAllocatorCount == 0;
            result.voiceGraph = graph;
            return result;
        }

        if (incomingCount > 1)
        {
            result.errorMessage = "instance.mix must have at most one connection into its 'in' port (found "
                                   + juce::String (incomingCount) + ")";
            return result;
        }

        std::unordered_map<juce::String, std::vector<juce::String>> successorsOf, predecessorsOf;
        for (const auto& connection : connections)
        {
            successorsOf[connection.fromNodeId].push_back (connection.toNodeId);
            predecessorsOf[connection.toNodeId].push_back (connection.fromNodeId);
        }

        // Backward from instance.mix (excluding itself) = voice domain.
        const auto voiceDomain = reachableFollowing (instanceMixId, predecessorsOf, false);
        // Forward from instance.mix (including itself) = global domain.
        auto globalDomain = reachableFollowing (instanceMixId, successorsOf, true);

        // M21: mono sources. A node that is in neither set but feeds the global
        // domain (io.audioIn, io.control, io.transport, a constant... feeding a
        // chain after the mix) is not per-voice and not downstream of the mix,
        // yet DOMAINS.md §2 is explicit that mono signals are free everywhere.
        // Walk backward from every global node; whatever that reaches outside
        // the voice domain is a mono source and joins the global plan.
        // (Anything that reaches the mix backward is voice domain already, and
        // an unconnected node reaches nothing, so it is still rejected below.)
        {
            std::vector<juce::String> stack (globalDomain.begin(), globalDomain.end());
            std::unordered_set<juce::String> visited (globalDomain.begin(), globalDomain.end());

            while (! stack.empty())
            {
                const auto current = stack.back();
                stack.pop_back();

                const auto it = predecessorsOf.find (current);
                if (it == predecessorsOf.end())
                    continue;

                for (const auto& previous : it->second)
                {
                    if (voiceDomain.count (previous) > 0)
                        continue; // a voice node feeding the global domain: checked below, not a mono source
                    if (visited.insert (previous).second)
                    {
                        globalDomain.insert (previous);
                        stack.push_back (previous);
                    }
                }
            }
        }

        // 09-28-InstanceAllocator.1 (part 4): a node reachable from NEITHER
        // domain is either a freshly-placed, not-yet-wired node (the editor
        // always places a node before wiring it — the exact same "must be
        // placeable on its own" reasoning the instance.mix incomingCount==0
        // carve-out above already applies to instance.mix itself) or a
        // little cluster of such nodes wired only to each other. Used to be
        // a hard compile error the instant instance.mix had a real upstream
        // connection ("Node 'X' is not connected to either the voice or
        // global domain") — found live: placing ANY new node became
        // impossible the moment a real bridged graph existed, since a
        // brand-new node starts with zero connections by construction. Fold
        // every such node (and anything only reachable through other such
        // nodes) into the global domain instead — it already runs
        // unconditionally every block, so this makes an orphan inert
        // (contributes nothing to final output, exactly right for
        // something not wired to anything yet) while keeping it fully
        // compiled and inspectable via a tap immediately, matching how the
        // instanceMixCount==0 branch above already treats an unconnected
        // instance.allocator. Never poaches an already-earned voiceDomain
        // membership.
        std::unordered_set<juce::String> orphanNodeIds;
        {
            std::vector<juce::String> stack;
            for (const auto& node : nodes)
                if (voiceDomain.count (node.id) == 0 && globalDomain.count (node.id) == 0)
                    if (orphanNodeIds.insert (node.id).second)
                        stack.push_back (node.id);

            while (! stack.empty())
            {
                const auto current = stack.back();
                stack.pop_back();

                auto tryAdd = [&] (const juce::String& id)
                {
                    if (voiceDomain.count (id) > 0 || globalDomain.count (id) > 0)
                        return; // never poach a domain a node already legitimately earned
                    if (orphanNodeIds.insert (id).second)
                        stack.push_back (id);
                };

                const auto itFwd = successorsOf.find (current);
                if (itFwd != successorsOf.end())
                    for (const auto& next : itFwd->second)
                        tryAdd (next);

                const auto itBack = predecessorsOf.find (current);
                if (itBack != predecessorsOf.end())
                    for (const auto& previous : itBack->second)
                        tryAdd (previous);
            }

            for (const auto& id : orphanNodeIds)
                globalDomain.insert (id);
        }

        // DOMAINS.md §2/§7: poly -> mono needs a Voice Mix, "no implicit
        // summing anywhere, ever". Before M21 such an edge was silently
        // dropped when the global graph was built, leaving its target reading
        // silence with no indication why; say so instead. The one legitimate
        // edge from the voice domain into the global one is into the mix.
        // (Runs after the orphan fold above, so an orphan that turns out to
        // be wired straight to a voice-domain node without going through
        // instance.mix is still correctly caught here, not silently allowed.)
        for (const auto& connection : connections)
        {
            if (voiceDomain.count (connection.fromNodeId) == 0 || globalDomain.count (connection.toNodeId) == 0)
                continue;
            if (connection.toNodeId == instanceMixId && connection.toPortId == instanceMixInputPortId)
                continue;

            result.errorMessage = "Node '" + connection.fromNodeId + "' (voice domain) feeds node '" + connection.toNodeId
                                   + "' (global domain) directly: a voice-domain signal reaches the global domain only through"
                                   + " instance.mix. If '" + connection.fromNodeId + "' is a mono source (audio in, MIDI control,"
                                   + " transport) that feeds both, use a separate one for each domain.";
            return result;
        }

        for (const auto& node : nodes)
        {
            const auto inVoice = voiceDomain.count (node.id) > 0;
            const auto inGlobal = globalDomain.count (node.id) > 0;

            if (inVoice && inGlobal)
            {
                result.errorMessage = "Node '" + node.id
                                       + "' is reachable from both domains (a cycle through instance.mix?)";
                return result;
            }

            // An orphaned instance.allocator (folded into globalDomain just
            // above) is legitimately not yet wired to anything - not the
            // "connected the wrong way" case this check exists to catch.
            if (node.type == instanceAllocatorTypeId && ! inVoice && orphanNodeIds.count (node.id) == 0)
            {
                result.errorMessage = "instance.allocator node '" + node.id
                                       + "' must be in the voice domain (upstream of instance.mix)";
                return result;
            }
        }

        if (globalDomain.count (graph.getOutputNodeId()) == 0)
        {
            result.errorMessage = "Graph output node '" + graph.getOutputNodeId()
                                   + "' must be in the global domain when an instance.mix node is present";
            return result;
        }

        NodeGraph voiceGraph;
        for (const auto& node : nodes)
            if (voiceDomain.count (node.id) > 0)
                voiceGraph.addNode (node);

        for (const auto& connection : connections)
            if (voiceDomain.count (connection.fromNodeId) > 0 && voiceDomain.count (connection.toNodeId) > 0)
                voiceGraph.addConnection (connection);

        voiceGraph.setOutput (incoming->fromNodeId, incoming->fromPortId);

        NodeGraph globalGraph;
        for (const auto& node : nodes)
            if (globalDomain.count (node.id) > 0)
                globalGraph.addNode (node);

        for (const auto& connection : connections)
            if (globalDomain.count (connection.fromNodeId) > 0 && globalDomain.count (connection.toNodeId) > 0)
                globalGraph.addConnection (connection);
        // The edge into instance.mix's "in" port is naturally excluded
        // above — its source lives in voiceDomain, not globalDomain — no
        // explicit filtering needed; InstanceMixNode reads Silence there
        // and gets its real input via setExternalBlock() instead.

        globalGraph.setOutput (graph.getOutputNodeId(), graph.getOutputPortId());

        result.success = true;
        result.hasGlobalDomain = true;
        result.instanceMixNodeId = instanceMixId;
        result.voiceGraph = std::move (voiceGraph);
        result.globalGraph = std::move (globalGraph);
        return result;
    }
}
