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
        constexpr const char* instanceVoiceTypeId = "instance.allocate.voice"; // 09-28-InstanceAllocator.3 — renamed from "instance.allocator"; 09-29-AddMenu.1 — renamed again from "instance.voice"

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
        juce::String instanceVoiceId;
        int instanceMixCount = 0;
        int instanceVoiceCount = 0;
        for (const auto& node : nodes)
        {
            if (node.type == instanceMixTypeId)
            {
                instanceMixId = node.id;
                ++instanceMixCount;
            }
            else if (node.type == instanceVoiceTypeId)
            {
                instanceVoiceId = node.id;
                ++instanceVoiceCount;
            }
        }

        if (instanceMixCount == 0)
        {
            if (instanceVoiceCount == 0)
            {
                result.success = true;
                result.hasGlobalDomain = false;
                result.monoOnly = true;
                result.voiceGraph = graph;
                return result;
            }

            // 09-28-InstanceAllocator.1: an instance.allocate.voice that genuinely
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
            auto voiceReachable = reachableFollowing (instanceVoiceId, successorsOf, true);
            for (const auto& id : reachableFollowing (instanceVoiceId, predecessorsOf, false))
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
            voiceOnly.setOutput (instanceVoiceId, "gate");

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

        if (instanceVoiceCount > 1)
        {
            result.errorMessage = "Only one instance.allocate.voice node is supported per graph (found "
                                   + juce::String (instanceVoiceCount)
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
            result.monoOnly = instanceVoiceCount == 0;
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
        auto voiceDomain = reachableFollowing (instanceMixId, predecessorsOf, false);
        // Forward from instance.mix (including itself) = global domain.
        auto globalDomain = reachableFollowing (instanceMixId, successorsOf, true);

        // M21's original "mono sources" backward-expansion used to live here
        // as its own separate pass (walk backward from every global node;
        // whatever that reaches outside the voice domain joins the global
        // plan too — io.audioIn, io.control, a constant feeding a chain
        // after the mix, etc., per DOMAINS.md §2's "mono signals are free
        // everywhere"). Folded into the unified classification below
        // instead (09-28-InstanceAllocator.1 part 4): that pass already
        // implements the exact same "not fed by voice -> global" rule for
        // every not-yet-classified node, PLUS the "fed by voice -> voice"
        // half M21's own version never had — a real bug found live via this
        // milestone's own new test (an orphan fed by the voice domain that
        // ALSO happens to feed something already in the global domain, e.g.
        // wired straight to the designated output, used to get silently
        // pre-claimed as global by walking backward from that existing
        // global node BEFORE this next pass could see it was voice-fed).
        // Keeping two separate passes for what's really one rule meant they
        // could race and disagree; one pass now answers the question once.

        // 09-28-InstanceAllocator.1 (part 4): a node reachable from NEITHER
        // domain is either a freshly-placed, not-yet-wired node (the editor
        // always places a node before wiring it — the exact same "must be
        // placeable on its own" reasoning the instance.mix incomingCount==0
        // carve-out above already applies to instance.mix itself), a little
        // cluster of such nodes wired only to each other, or — the case
        // that broke this fix's first cut, found live — a node ALREADY fed
        // by the voice domain (e.g. logic.select's condition wired straight
        // from instance.allocate.voice's gate) but not yet wired onward to
        // anything that reaches instance.mix. Used to be a hard compile
        // error the instant instance.mix had a real upstream connection
        // ("Node 'X' is not connected to either the voice or global
        // domain"). First cut of this part's own fix always folded such a
        // node into the global domain — fine for a genuinely disconnected
        // one, but wrong for the fed-by-voice case: reclassifying it as
        // global made its own real incoming edge FROM the voice domain
        // look like a straight voice->global violation, rejecting the
        // single most ordinary construction order (wire a cable in, then
        // wire the next one). Fixed by classifying each not-yet-connected-
        // to-either-domain cluster on its own: if ANY edge feeds into the
        // cluster from the voice domain, the WHOLE cluster joins the voice
        // domain instead (DOMAINS.md §2's rule is asymmetric — voice
        // content may only reach global content through instance.mix, but
        // nothing stops it drifting deeper into the voice domain first);
        // everything else (no connections at all, or fed only by the
        // global domain / other such clusters) still joins the global
        // domain as before — inert until wired further, but fully compiled
        // and inspectable via a tap immediately, matching how the
        // instanceMixCount==0 branch above already treats an unconnected
        // instance.allocate.voice. Never poaches an already-earned membership.
        std::unordered_set<juce::String> foldedNodeIds;
        {
            std::unordered_set<juce::String> unclassified;
            for (const auto& node : nodes)
                if (voiceDomain.count (node.id) == 0 && globalDomain.count (node.id) == 0)
                    unclassified.insert (node.id);

            while (! unclassified.empty())
            {
                // One connected component, following only edges between two
                // still-unclassified nodes — an edge that already touches a
                // real domain is this component's BOUNDARY, not its interior.
                std::unordered_set<juce::String> component;
                std::vector<juce::String> stack { *unclassified.begin() };
                component.insert (*unclassified.begin());

                while (! stack.empty())
                {
                    const auto current = stack.back();
                    stack.pop_back();

                    auto tryAdd = [&] (const juce::String& id)
                    {
                        if (unclassified.count (id) == 0)
                            return; // a real domain, or already claimed by an earlier component
                        if (component.insert (id).second)
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

                auto fedByVoice = false;
                for (const auto& connection : connections)
                {
                    if (component.count (connection.toNodeId) > 0 && voiceDomain.count (connection.fromNodeId) > 0)
                    {
                        fedByVoice = true;
                        break;
                    }
                }

                for (const auto& id : component)
                {
                    if (fedByVoice)
                        voiceDomain.insert (id);
                    else
                        globalDomain.insert (id);
                    foldedNodeIds.insert (id);
                    unclassified.erase (id);
                }
            }
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

            // An instance.allocate.voice folded by the block just above (almost
            // always into globalDomain, since it has nothing feeding it by
            // definition — an allocator has no real inputs of its own to be
            // "fed by voice" through) is legitimately not yet wired to
            // anything downstream - not the "connected the wrong way" case
            // this check exists to catch.
            if (node.type == instanceVoiceTypeId && ! inVoice && foldedNodeIds.count (node.id) == 0)
            {
                result.errorMessage = "instance.allocate.voice node '" + node.id
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
