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
                // wiki/plans/StereoChannels.md §3: per-channel processors are
                // Inherited now, so this only happens for a port that is
                // genuinely one signal (a detector, an exciter, a bridge).
                // Reducing stereo loses information, so it is never silent:
                // the user picks how, and the choice becomes a visible
                // mix.downmix node in that mode.
                CanConnectResult result;
                result.outcome = ConnectionOutcome::NeedsAdapters;
                result.adapterChain = { AdapterStep { "mix.downmix", "in" } };
                result.reason = "Stereo into a mono-only port: choose Mid, Left, Right or Side";
                result.choices = { "mid", "left", "right", "side" };
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
                step.seedFromSourceRange = true;      // input range: the source's own bounds, else its polarity
                step.seedFromDestinationRange = true; // output range: the destination's
                return needsAdapter (step, "Modulation-range value into a real-quantity port needs a Map");
            }

            if (isRealQuantity (from.quantity) && isNormalisedQuantity (to.quantity))
            {
                AdapterStep step { "adapt.normalise", "in" };
                step.seedFromSourceRange = true;
                return needsAdapter (step, "Real-quantity value into a modulation-range port needs a Normalise");
            }

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
            if (from.quantity == Quantity::Pitch && to.quantity == Quantity::Frequency)
            {
                AdapterStep step { "adapt.pitchToFrequency", "pitch" };
                step.outputPortId = "frequency";
                return needsAdapter (step, "Pitch into a Frequency-typed port needs an exact conversion, not a linear remap");
            }
            if (from.quantity == Quantity::Frequency && to.quantity == Quantity::Pitch)
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
            AdapterStep step { "adapt.map", "in" };
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

        // Audio -> Control Bridge (wiki/plans/AudioControlBridge.md). This
        // deliberately revises ADR-0019's original text (which named
        // `env.follower` as the eventual auto-insert target for this pair,
        // a wave that was never executed) — `env.follower` throws away the
        // waveform on purpose and stays real, correct, and hand-placed only
        // for sidechain/ducking-style patches; auto-inserting it here would
        // silently defeat the actual motivating use case (FM/ring-mod/
        // audio-rate parameter modulation, where the instantaneous waveform
        // value IS the modulator). See the plan's §5 for the full reasoning
        // and archive_docs/decisions/0019-adapter-table.md's Amendment.
        if (from.type == SignalType::Audio && to.type == SignalType::Control)
        {
            // Stereo stays a hard reject in v1 (plan §6) — a stereo source
            // into a real-quantity Control port would need mix.downmix +
            // adapt.audioToControl + adapt.map, three adapters deep, over
            // this codebase's own "at most two, or reject" ceiling
            // (wiki/NODES.System.md §4).
            if (from.channels == Channels::Stereo)
                return reject ("Stereo source into a Control-typed port needs mix.downmix first");

            AdapterStep first { "adapt.audioToControl", "in" };

            if (isRealQuantity (to.quantity))
            {
                // Two-step chain: cross the type wall (Bipolar), then
                // rescale into the destination's real quantity — mirrors
                // connectControl()'s own Unipolar/Bipolar -> real-quantity
                // case above, just reached from Audio instead of from an
                // existing Control source.
                AdapterStep second { "adapt.map", "in" };
                second.seedFromSourceRange = true; // from adapt.audioToControl's own Bipolar output, not the Audio source
                second.seedFromDestinationRange = true;

                CanConnectResult result;
                result.outcome = ConnectionOutcome::NeedsAdapters;
                result.adapterChain = { first, second };
                result.reason = "Raw audio into a real-quantity port needs Audio to Modulation, then Map";
                return result;
            }

            return needsAdapter (first, "A raw audio signal into a modulation port needs Audio to Modulation");
        }

        // Control -> Audio Bridge (wiki/plans/ControlToAudioBridge.md) — the
        // reverse of the Audio -> Control bridge above, closing the "open
        // symmetric question for later" AudioControlBridge.md §6 explicitly
        // deferred. No stereo complication here at all: Control ports have
        // no Channels concept to begin with, so there's no reject case to
        // design the way the forward direction needed one for a stereo
        // Audio source.
        if (from.type == SignalType::Control && to.type == SignalType::Audio)
        {
            if (isRealQuantity (from.quantity))
            {
                // Real quantity -> Audio needs the SOURCE's own range
                // collapsed to Unipolar first (adapt.normalise, seeded from
                // the SOURCE range — the exact same node/seeding
                // connectControl() already uses for Control(real) ->
                // Control(Unipolar/Bipolar) above), then the bridge. Mirrors
                // adapt.audioToControl -> adapt.map's own two-step shape,
                // just with the real-quantity step on the source side this
                // time instead of the destination side.
                AdapterStep first { "adapt.normalise", "in" };
                first.seedFromSourceRange = true;
                AdapterStep second { "adapt.controlToAudio", "in" };

                CanConnectResult result;
                result.outcome = ConnectionOutcome::NeedsAdapters;
                result.adapterChain = { first, second };
                result.reason = "A real-quantity modulation source into Audio needs Normalise, then To Audio";
                return result;
            }

            AdapterStep step { "adapt.controlToAudio", "in" };
            return needsAdapter (step, "A modulation signal into an Audio-typed port needs To Audio");
        }

        // Direct feedback: "bool not being pluggable into control and
        // ints... annoying." Exactly as mechanical/opinion-free as every
        // other adapter above — a plain two-value lookup, never a creative
        // choice — so it gets the same auto-insertion treatment.
        if (from.type == SignalType::Boolean && to.type == SignalType::Control)
        {
            AdapterStep step { "adapt.boolToControl", "in" };
            return needsAdapter (step, "A Boolean signal into a Control-typed port needs a From Bool");
        }

        return reject ("Incompatible signal types with no adapter available yet");
    }
}
