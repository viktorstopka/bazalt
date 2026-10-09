#include "bazalt/engine/graph/CanConnect.h"
#include <optional>

namespace bazalt::engine
{
    namespace
    {
        // An audio waveform reads as a normalised ±1 value; a Boolean is a
        // plain 0/1 that fits anywhere (meaningOf below).
        bool isNormalisedQuantity (Quantity q) noexcept
        {
            return q == Quantity::Unipolar || q == Quantity::Bipolar || q == Quantity::Audio;
        }

        bool isRealQuantity (Quantity q) noexcept
        {
            return q != Quantity::Dimensionless && q != Quantity::Boolean && ! isNormalisedQuantity (q);
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

        /** wiki/plans/DataAndWavetable.md D1: Audio, Control and Boolean are one
            numeric signal — the same per-sample float buffers in the engine. What
            differs is only what a value means, so connecting them is decided by
            meaning: an Audio port reads as a waveform (±1, like Bipolar), a
            Boolean as a plain 0/1 that fits anywhere, a Control port by its own
            quantity. */
        bool isValueType (SignalType type) noexcept
        {
            return type == SignalType::Signal;
        }

        Quantity meaningOf (const PortDescriptor& port) noexcept
        {
            if (port.quantity == Quantity::Audio)
                return Quantity::Bipolar;
            if (port.quantity == Quantity::Boolean)
                return Quantity::Dimensionless;
            return port.quantity;
        }

        /** What a destination expects. A port that takes on both the type and
            the quantity of what feeds it (Multiply, Add, Clip's `in`, Reroute)
            expects nothing of its own — whatever it resolved to was borrowed
            from its inputs, so a Gain into a Multiply already carrying audio is
            just a product, never a range to rescale. */
        Quantity expectedMeaningOf (const PortDescriptor& port) noexcept
        {
            if (port.polymorphism == PortPolymorphism::SignalAndQuantity)
                return Quantity::Dimensionless;
            return meaningOf (port);
        }

        AdapterStep mapStep()
        {
            AdapterStep step { "adapt.map", "in" };
            step.seedFromSourceRange = true;      // input range: the source's own bounds, else its polarity
            step.seedFromDestinationRange = true; // output range: the destination's
            return step;
        }

        /** The rescaling a connection needs between two meanings, if any. */
        std::optional<CanConnectResult> rescaleFor (Quantity from, Quantity to)
        {
            if (from == to || from == Quantity::Dimensionless || to == Quantity::Dimensionless)
                return std::nullopt;

            // Two normalised ranges (an audio waveform, a modulation) meet directly.
            if (isNormalisedQuantity (from) && isNormalisedQuantity (to))
                return std::nullopt;

            if (isNormalisedQuantity (from) && isRealQuantity (to))
                return needsAdapter (mapStep(), "A modulation or audio value into a real-quantity port needs a Map");

            if (isRealQuantity (from) && isNormalisedQuantity (to))
                return needsAdapter (mapStep(), "A real-quantity value into a modulation or audio port needs a Map");

            // Pitch <-> Frequency specifically: NOT the generic linear remap
            // below. The two are exponentially related (each semitone is
            // ×2^(1/12)), so a linear interpolation between two seeded
            // endpoints is quietly wrong for every pitch in between — a
            // real correctness gap in the "MVP, linear-only for now" remap
            // path (direct feedback caught this; it revises this function's
            // own long-standing Pitch-into-Cutoff example, not just adds a
            // new case). `adapt.pitchToFrequency`/`adapt.frequencyToPitch`
            // are the actual, exact conversion — one step, no seeding
            // needed at all (the formula is fixed, not range-dependent).
            if (from == Quantity::Pitch && to == Quantity::Frequency)
            {
                AdapterStep step { "adapt.pitchToFrequency", "pitch" };
                step.outputPortId = "frequency";
                return needsAdapter (step, "Pitch into a Frequency-typed port needs an exact conversion, not a linear remap");
            }
            if (from == Quantity::Frequency && to == Quantity::Pitch)
            {
                AdapterStep step { "adapt.frequencyToPitch", "frequency" };
                step.outputPortId = "pitch";
                return needsAdapter (step, "Frequency into a Pitch-typed port needs an exact conversion, not a linear remap");
            }

            // Two different real quantities (e.g. Frequency and Time) — not
            // in SIGNAL_TYPES.md §5's original ten-pair table, but both
            // sides are still real numeric ranges, so insert `adapt.map`
            // (NODE_CATALOG.md's own node — this is its MVP linear form,
            // curve support grows it later rather than replacing it),
            // seeded from BOTH ends at once: inMin/inMax from the source's
            // own range, outMin/outMax from the destination's. A real,
            // generic real-to-real rescale — not a guessed conversion
            // between the two quantities' meanings, and not hidden inside
            // the wire: the adapter is an ordinary, visible, editable node
            // once auto-inserted (GraphEditController::connectWithAutoAdapt),
            // exactly like every other adapter here. Direct feedback:
            // rejecting this pair outright was an unfinished case, not a
            // deliberate design choice — Pitch<->Frequency specifically no
            // longer falls through to here, see above.
            return needsAdapter (mapStep(), "Different real quantities — rescaled via Map");
        }

        CanConnectResult connectValues (const PortDescriptor& from, const PortDescriptor& to)
        {
            // Stereo into a port that takes one channel throws information
            // away, so it is never silent: the user picks how, and the choice
            // becomes a visible Downmix (wiki/plans/StereoChannels.md §3) —
            // followed by a Map when the destination is a real quantity.
            const auto reducesStereo = from.channels == Channels::Stereo && to.channels == Channels::Mono;
            auto rescale = rescaleFor (meaningOf (from), expectedMeaningOf (to));

            if (! reducesStereo)
                return rescale.has_value() ? *rescale : ok();

            CanConnectResult result;
            result.outcome = ConnectionOutcome::NeedsAdapters;
            result.adapterChain = { AdapterStep { "mix.downmix", "in" } };
            result.reason = "Stereo into a mono-only port: choose Mid, Left, Right or Side";
            result.choices = { "mid", "left", "right", "side" };
            if (rescale.has_value())
            {
                // Downmix hands on an audio-range signal, so what follows is
                // always a Map (never Pitch <-> Frequency).
                if (rescale->adapterChain.size() != 1 || rescale->adapterChain.front().typeId != "adapt.map")
                    return reject ("Stereo into this port needs a Downmix first");
                result.adapterChain.push_back (mapStep());
            }
            return result;
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

        if (isValueType (from.type) && isValueType (to.type))
            return connectValues (from, to);

        if (from.type == to.type)
        {
            switch (from.type)
            {
                case SignalType::Event:    return ok();
                case SignalType::Note:     return ok();
                case SignalType::Spectral: return reject ("Spectral is reserved, not yet implemented");
                case SignalType::Signal:
                case SignalType::Data:     break; // handled above
            }
            return reject ("Unhandled signal type");
        }

        // A value into an Event port: a Threshold turns crossings into moments.
        if (isValueType (from.type) && to.type == SignalType::Event)
        {
            AdapterStep step { "adapt.threshold", "by" };
            return needsAdapter (step, "A value into an Event-typed port needs a Threshold");
        }

        return reject ("Incompatible signal types with no adapter available yet");
    }
}
