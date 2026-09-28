#include "bazalt/engine/graph/CanConnect.h"

namespace bazalt::engine
{
    namespace
    {
        bool isNormalisedQuantity (Quantity q) noexcept
        {
            return q == Quantity::Unipolar || q == Quantity::Bipolar;
        }

        bool isRealQuantity (Quantity q) noexcept
        {
            return q != Quantity::Dimensionless && ! isNormalisedQuantity (q);
        }

        CanConnectResult ok() noexcept
        {
            // NOT `return {};` — CanConnectResult's default member
            // initializer is deliberately Reject (the safe default for
            // anyone who forgets to set it explicitly), so a bare `{}`
            // here would silently construct a Reject, not an Ok. This is
            // exactly the bug that shipped first: every legitimate
            // connection in the codebase got rejected, `prepare()` failed
            // silently (jassert without a debugger attached), and every
            // downstream processBlock() call dereferenced a never-
            // published plan — a real segfault, caught immediately by the
            // full test suite going from 111 green to 21 failing/crashing.
            CanConnectResult result;
            result.outcome = ConnectionOutcome::Ok;
            return result;
        }

        CanConnectResult reject (juce::String reason)
        {
            CanConnectResult result;
            result.outcome = ConnectionOutcome::Reject;
            result.reason = std::move (reason);
            return result;
        }

        CanConnectResult needsAdapter (AdapterStep step, juce::String reason)
        {
            CanConnectResult result;
            result.outcome = ConnectionOutcome::NeedsAdapters;
            result.adapterChain = { std::move (step) };
            result.reason = std::move (reason);
            return result;
        }

        CanConnectResult connectAudio (const PortDescriptor& from, const PortDescriptor& to)
        {
            if (from.channels == Channels::Inherited || to.channels == Channels::Inherited)
                return ok();

            if (from.channels == Channels::Stereo && to.channels == Channels::Mono)
            {
                // Real stereo cable redesign (wiki/NODES.System.md §9):
                // `mix.downmix` is now a genuine 1-in-1-out node (one real
                // `Channels::Stereo` "in" port, one mono "out"), so this
                // fits the same single-`AdapterStep` splice mechanism
                // `adapt.map`/`adapt.normalise`/`adapt.threshold` already
                // use — `GraphEditController::connectWithAutoAdapt` auto-
                // inserts it now, closing the gap this comment used to flag
                // (downmix used to be 2-in-1-out, which never fit).
                CanConnectResult result;
                result.outcome = ConnectionOutcome::NeedsAdapters;
                result.adapterChain = { AdapterStep { "mix.downmix", "in" } };
                result.reason = "Stereo source into a mono-only port needs mix.downmix";
                return result;
            }

            return ok(); // mono->mono, mono->stereo (free broadcast), stereo->stereo
        }

        CanConnectResult connectControl (const PortDescriptor& from, const PortDescriptor& to)
        {
            // No `rate` field exists yet (SIGNAL_TYPES.md §9 open question
            // #3) — every Control-Control pair is judged on quantity alone
            // until a future milestone adds it.
            if (from.quantity == to.quantity
                || from.quantity == Quantity::Dimensionless
                || to.quantity == Quantity::Dimensionless)
            {
                return ok();
            }

            if (isNormalisedQuantity (from.quantity) && isRealQuantity (to.quantity))
            {
                AdapterStep step { "adapt.map", "in" };
                step.seedFromDestinationRange = true;
                return needsAdapter (step, "Modulation-range value into a real-quantity port needs a Map");
            }

            if (isRealQuantity (from.quantity) && isNormalisedQuantity (to.quantity))
            {
                AdapterStep step { "adapt.normalise", "in" };
                step.seedFromSourceRange = true;
                return needsAdapter (step, "Real-quantity value into a modulation-range port needs a Normalise");
            }

            // Two different real quantities (e.g. Frequency and Pitch, or
            // Frequency and Time) — not in SIGNAL_TYPES.md §5's original
            // ten-pair table, but both sides are still real numeric ranges,
            // so insert `adapt.remap` (NODE_CATALOG.md's own node — this is
            // its MVP linear form, curve support grows it later rather than
            // replacing it), seeded from BOTH ends at once: inMin/inMax
            // from the source's own range, outMin/outMax from the
            // destination's. A real, generic real-to-real rescale — not a
            // guessed conversion between the two quantities' meanings, and
            // not hidden inside the wire: the adapter is an ordinary,
            // visible, editable node once auto-inserted
            // (GraphEditController::connectWithAutoAdapt), exactly like
            // every other adapter here. Direct feedback: rejecting this
            // pair (e.g. Pitch into a filter's Cutoff — pitch-tracking, a
            // standard synthesis technique) was an unfinished case, not a
            // deliberate design choice.
            AdapterStep step { "adapt.remap", "in" };
            step.seedFromSourceRange = true;
            step.seedFromDestinationRange = true;
            return needsAdapter (step, "Different real quantities — remapped via Remap");
        }
    }

    CanConnectResult canConnect (const PortDescriptor& from, const PortDescriptor& to)
    {
        if (from.type == SignalType::Data || to.type == SignalType::Data)
        {
            if (from.type != SignalType::Data || to.type != SignalType::Data)
                return reject ("Data never converts implicitly (SIGNAL_TYPES.md §5)");

            const auto producedTag = from.dataTags.empty() ? DataTag::Unknown : from.dataTags.front();
            for (const auto accepted : to.dataTags)
                if (dataTagAccepted (producedTag, accepted))
                    return ok();

            return reject ("Data tag mismatch — this port doesn't accept what's produced here");
        }

        if (from.type == to.type)
        {
            switch (from.type)
            {
                case SignalType::Audio:    return connectAudio (from, to);
                case SignalType::Control:  return connectControl (from, to);
                case SignalType::Event:    return ok();
                case SignalType::Note:     return ok();
                case SignalType::Boolean:  return ok();
                case SignalType::Spectral: return reject ("Spectral is reserved, not yet implemented");
                case SignalType::Data:     break; // handled above
            }
            return reject ("Unhandled signal type");
        }

        // Heterogeneous pairs this milestone actually ships an adapter for
        // (ADR-0019's incremental plan — everything else in
        // SIGNAL_TYPES.md §5's table is a real, future Reject until its
        // adapter node exists, not a chain this function can't back).
        if (from.type == SignalType::Control && to.type == SignalType::Event)
        {
            AdapterStep step { "adapt.threshold", "by" };
            return needsAdapter (step, "A Control signal into an Event-typed port needs a Threshold");
        }

        return reject ("Incompatible signal types with no adapter available yet");
    }
}
