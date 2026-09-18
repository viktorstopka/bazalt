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
                // NOTE: no real stereo-output node exists yet — every port
                // today is a single mono buffer (AlignedBuffer/ExecutionPlan
                // never allocate more than one channel per port). This
                // branch is correct as a *rule*, but `mix.downmix`'s real
                // shape (two separate mono inputs, "left"/"right") can't be
                // driven by a single `AdapterStep` the way `adapt.map`/
                // `adapt.normalise`/`adapt.threshold` can — those are
                // genuine 1-in-1-out transformers; downmix is 2-in-1-out.
                // Auto-insertion for this specific case is therefore not
                // attempted yet (`GraphEditController::connectWithAutoAdapt`
                // rejects it with a message pointing at manual insertion) —
                // flagged honestly rather than claiming a chain shape that
                // doesn't fit. Revisit once a real multi-channel-per-port
                // buffer exists and/or once a real stereo-output node needs
                // this for real (M22+).
                CanConnectResult result;
                result.outcome = ConnectionOutcome::NeedsAdapters;
                result.adapterChain = { AdapterStep { "mix.downmix", "left" } }; // informational only, see above
                result.reason = "Stereo source into a mono-only port needs mix.downmix (manual insertion for now)";
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

            // Two different real quantities (e.g. Frequency into Time), or
            // Unipolar into Bipolar / vice versa: not in SIGNAL_TYPES.md
            // §5's table and not resolvable by Map/Normalise (those are
            // specifically about the normalised<->real boundary, not
            // real<->real or normalised<->normalised). Reject rather than
            // guess at a conversion the design docs never specified —
            // flagged as a real, open gap (RECONCILIATION.md-style), not
            // silently allowed or silently invented.
            return reject ("Incompatible Control quantities with no defined adapter");
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
