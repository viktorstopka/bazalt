#pragma once

#include "bazalt/engine/graph/PortDescriptor.h"
#include <vector>

namespace bazalt::engine
{
    enum class ConnectionOutcome
    {
        Ok,
        NeedsAdapters,
        Reject
    };

    /** One node to splice into a `NeedsAdapters` chain (SIGNAL_TYPES.md
        §5). `inputPortId` is which of the adapter's own input ports
        receives the incoming signal — not always "in" (`adapt.threshold`
        uses "by") — so the caller doesn't have to special-case it per
        adapter type. `seedFromSourceRange`/`seedFromDestinationRange`
        tell the caller which side's `minValue`/`maxValue` to seed the
        adapter's own structural range from (SIGNAL_TYPES.md §5's
        "Seeding" column) — never both, never neither, for the adapters
        this milestone ships.
    */
    struct AdapterStep
    {
        juce::String typeId;
        juce::String inputPortId;
        juce::String outputPortId = "out";
        bool seedFromSourceRange = false;
        bool seedFromDestinationRange = false;
    };

    struct CanConnectResult
    {
        ConnectionOutcome outcome = ConnectionOutcome::Reject;

        /** 1-2 entries iff `outcome == NeedsAdapters` (SIGNAL_TYPES.md §5:
            "a chain is at most two adapters"); always empty otherwise.
        */
        std::vector<AdapterStep> adapterChain;

        /** Human-readable — the `CommandResult::errorMessage`/compile
            error a `Reject` (or, informationally, a `NeedsAdapters`)
            surfaces. Empty iff `outcome == Ok`.
        */
        juce::String reason;

        /** Non-empty when the adapter chain throws information away and the
            user must say how (wiki/plans/StereoChannels.md §3 — stereo into a
            genuinely mono port: Mid, Left, Right or Side). Each entry is an
            enum option id of the first adapter's mode parameter; nothing is
            ever auto-inserted for such a connection without one of them.
        */
        std::vector<juce::String> choices;
    };

    /** SIGNAL_TYPES.md §4: "the engine is the authority, and there is
        exactly one implementation." A pure function of two port
        descriptors — no engine state, no I/O, safe to call from anywhere
        (compile time, a command handler, a future UI-facing query). `from`
        must be an output port, `to` an input port; the caller (whoever
        already resolved these from a node's `getOutputPorts()`/
        `getInputPorts()`) is responsible for that — this function only
        looks at shape, never direction, since `PortDescriptor` alone
        doesn't carry which list it came from.

        Deliberately narrower than `SIGNAL_TYPES.md` §5's full ten-pair
        adapter table (ADR-0019's own "ship incrementally" plan): only
        pairs whose adapter node actually exists yet return
        `NeedsAdapters` — everything else this milestone can't back with a
        real, insertable node returns `Reject` with a reason saying so,
        never a chain it can't actually fulfil. As later milestones add
        adapter nodes (`adapt.envelopeFollower` M20, Note-typed adapters
        after M18, etc.), extend this function's branches then — the
        function's own source is the fixed, versioned fact of what's
        currently shippable, not a live registry query (that would break
        the "pure function, no engine state" contract).

        Also deliberately narrower than `rate` (block vs. audio,
        SIGNAL_TYPES.md §3/§9 open question #3): no `rate` field exists on
        `PortDescriptor` yet, so this function doesn't distinguish it —
        every `Control`-`Control` pair is judged on quantity/kind alone
        until a future milestone adds `rate` and this function grows a
        branch for it.
    */
    CanConnectResult canConnect (const PortDescriptor& from, const PortDescriptor& to);
}
