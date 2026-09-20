#pragma once

#include "bazalt/engine/graph/Data.h"
#include "bazalt/engine/graph/SignalType.h"
#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

namespace bazalt::engine
{
    /** kind: what shape of value this is (VALUE_MODEL.md §2) — independent
        of its physical meaning (`Quantity`) or how a UI gesture maps onto
        its range (`Curve`). Every existing port/parameter defaults to
        `Float`, matching what every one of them already is today.
    */
    enum class ValueKind
    {
        Float,
        Int,
        Bool,
        Enum
    };

    /** quantity: the physical dimension of a value (VALUE_MODEL.md §3) —
        what makes conversion, colouring, and formatting deterministic
        instead of heuristic. `Dimensionless` is the default: today's
        untyped ports/parameters all become `Dimensionless` until a node
        is migrated to declare a real one (`ValueTypes.h`'s factories are
        the migration path, one node at a time — see that file).
    */
    enum class Quantity
    {
        Dimensionless,
        Frequency,
        Pitch,
        Time,
        Gain,
        Ratio,
        Unipolar,
        Bipolar,
        Count,
        Phase
    };

    /** curve: how a 0-1 gesture maps onto a bounded value's range
        (VALUE_MODEL.md §2/§7) — perception, not storage; the value
        itself is always stored/transmitted in canonical units regardless
        of curve. Realised today via the pre-existing `skew`
        (ParameterDescriptor) / `isLogScale` (PortDescriptor) fields,
        which are kept as-is rather than replaced — `curve` is the
        UI-facing vocabulary the doc asks for, `skew`/`isLogScale` stay
        the mechanism. `CustomRef` is reserved for a future Data-backed
        curve (VALUE_MODEL.md §9 open question #2, M24's `data.table`)
        and has no meaning yet.
    */
    enum class Curve
    {
        Linear,
        Exponential,
        Logarithmic,
        CustomRef
    };

    /** polarity: meaningful only for normalised (`Unipolar`/`Bipolar`)
        quantities — VALUE_MODEL.md §3: "this distinction, not the signal
        type, is what the UI colours (orange for normalised, white for
        real units)." Irrelevant for a real-world quantity; left at its
        default there.
    */
    enum class Polarity
    {
        Unipolar,
        Bipolar
    };

    /** One choice in a `kind = Enum` value (VALUE_MODEL.md §2). `id` is a
        stable string and a patch-format foreign key exactly like a
        port/parameter/node type ID — never renamed once shipped.
        `label` is display-only and may be renamed freely. Reordering
        options must never change behaviour, so nothing may rely on
        index.
    */
    struct EnumOption
    {
        juce::String id;
        juce::String label;
    };

    /** A growable port group (SIGNAL_TYPES.md §6) — a node that
        conceptually takes N inputs of the same role (Mix, Add, Sum of
        modal excitations, a chord's interval offsets) declares one of
        these instead of a fixed port list. Concrete ports within the
        group are named `idPrefix + "0"`, `idPrefix + "1"`, ... and are
        stable: removing a middle port leaves a hole in the numbering,
        it never renumbers the others, so serialisation and existing
        connections survive a removal untouched.
    */
    struct PortGroup
    {
        juce::String idPrefix;
        int minCount = 2;
        int maxCount = 16;

        /** True (the default): the UI reveals a fresh empty port once
            the last one in the group is wired, capped at `maxCount`,
            never fewer than `minCount`. False: reserved for a future
            fixed-but-resizable-by-command policy; nothing produces this
            yet.
        */
        bool autoRevealOnLastConnected = true;
    };

    /** M16 — meaningful only for `type == SignalType::Audio` ports;
        `NODE_CATALOG (1).md`'s own decision: "an Audio port carries a
        mono signal... ports may declare channels: 1 | 2 | inherited."
        Not in `SIGNAL_TYPES.md` as originally written — a real, deliberate
        extension the bigger catalogue needed (ADR-0023). Every existing
        Audio port defaults to `Mono`, matching what every one of them
        actually is today. `canConnect` (CanConnect.h) treats mono->stereo
        as free (duplicated) and stereo->mono as `NeedsAdapters` via
        `mix.downmix`, exactly parallel to how `DOMAINS.md` treats
        mono/poly. `Inherited` is a forward-looking value for a future
        polymorphic-channel-count node — treated as "always compatible"
        by `canConnect` until a real node uses it.
    */
    enum class Channels
    {
        Mono,
        Stereo,
        Inherited
    };

    /** What a port inherits from its source on a polymorphic node (M21). */
    enum class PortPolymorphism
    {
        None,
        Quantity,          // takes the source's Quantity; its SignalType is fixed (logic.compare, adapt.sampleHold)
        SignalAndQuantity  // takes both (util.reroute, logic.select's data ports)
    };

    /** UI-facing metadata for one port, fully decoupled from the DSP
        implementation (ARCHITECTURE.md §3.6) — enough for a future UI to
        build a socket without knowing the node's C++ type. `id` is a
        stable, hand-assigned string, never an index, never renamed once
        shipped.

        Fields below `type` were added in M7 (NODE_EDITOR.md §3) — all
        defaulted so every M1/M2 node's existing `{id, type}`
        aggregate-initialization still compiles unchanged.
    */
    struct PortDescriptor
    {
        juce::String id;
        SignalType type;

        juce::String label;           // display name; falls back to `id` in the UI if empty
        bool isPrimaryOutput = false; // drives Alt-drag Mix/Add/Multiply and horizontal-node
                                       // output indicators (NODE_EDITOR.md §7's "primary output")

        // Numeric-value metadata (meaningful for Control/Boolean ports that
        // render as the UI's Value/Integer/Modulation/Boolean palette
        // entries, NODE_EDITOR.md §5) — left at defaults for Audio/Event
        // ports, which don't have a "value".
        juce::String unit;
        std::optional<float> minValue;
        std::optional<float> maxValue;
        float defaultValue = 0.0f;
        bool isInteger = false;
        bool isLogScale = false;

        /** An input port that models "a value you'd otherwise set with a
            knob, but can also be modulated over a cable" (NODE_EDITOR.md's
            dot/arrow port states — direct feedback: "the dot changes to the
            appropriate symbol... the slider disappears... the value falls
            back to the slider one" when disconnected). False (the default)
            means an unconnected input reads plain silence (0.0f), exactly
            like every port before this field existed — nothing changes for
            an existing port unless it opts in.

            When true, GraphCompiler feeds a NaN sentinel (not 0.0f, and not
            `defaultValue` — see GraphCompiler.cpp's own comment on why
            baking `defaultValue` in at compile time would break a node
            whose value is still meant to be adjustable via setParameter()
            after compilation) into this port's slot when nothing is wired
            to it. The node's own processSample/processBlock must check for
            NaN and substitute whatever it would otherwise have used (its
            own current parameter-driven value) in that case — DelayNode.h's
            "delay.line.samples" port is the first example and the pattern
            to copy for any other node doing this.
        */
        bool hasFallbackWhenUnconnected = false;

        // ---- Value contract (M14, VALUE_MODEL.md) --------------------------
        // Additive, all defaulted, appended after every field already used
        // positionally by an existing call site — same reasoning
        // isInteger/isLogScale/hasFallbackWhenUnconnected already established.
        // `RECONCILIATION.md` 1.1: kept as flat fields on both PortDescriptor
        // and ParameterDescriptor rather than one shared nested struct, since
        // most existing call sites use designated-initializer syntax on the
        // flat fields already (`{ .id = ..., .type = ... }`) and the two
        // structs' pre-existing min/max/default fields can't be folded into
        // a new nested type without breaking positional init everywhere.
        ValueKind kind = ValueKind::Float;
        Quantity quantity = Quantity::Dimensionless;
        Curve curve = Curve::Linear;
        Polarity polarity = Polarity::Unipolar;
        std::vector<EnumOption> enumOptions; // required iff kind == Enum
        float step = 0.0f;                   // 0 = continuous; kind == Int implies step 1
        std::optional<float> softMin;        // UI drag range; absent = same as minValue
        std::optional<float> softMax;        // (VALUE_MODEL.md §9 open question #3, added now
                                              // per the doc's own recommendation: cheap now,
                                              // expensive to retrofit once sliders exist)
        std::optional<PortGroup> group;      // set iff this port belongs to a growable group

        // ---- Data (M15, SIGNAL_TYPES.md §2/§3) -----------------------------
        // Meaningful only for a `type == SignalType::Data` port. Empty means
        // "not a Data port" for every existing port (unaffected by this
        // migration). Name and shape match SIGNAL_TYPES.md §3's own Port
        // pseudocode field exactly (`dataTags [..] // Data only`) — on an
        // INPUT port this is what it accepts (usually exactly one tag,
        // kept as a list for a future generic Data-preview node that would
        // accept several); on an OUTPUT port it's what the port actually
        // produces (always exactly one in practice). `dataTagAccepted()`
        // (Data.h) is the matching rule; `canConnect` (CanConnect.h) calls
        // it, not this field directly.
        std::vector<DataTag> dataTags;

        // ---- Channels (M16) -------------------------------------------------
        // Meaningful only for a `type == SignalType::Audio` port. See
        // `Channels`' own doc comment above.
        Channels channels = Channels::Mono;

        // ---- Polymorphism (M21) ---------------------------------------------
        /** How this port's declared type/quantity follow what is wired to its
            node (Node::hasPolymorphicPorts()); `None` for an ordinary port
            and for a port on a polymorphic node that stays fixed (select's
            Boolean `condition`). The type/quantity declared here are just the
            unconnected defaults. When several INPUT ports of one node are
            polymorphic, the one declared first wins if they disagree: the
            engine nodes apply that rule (InheritingPortsNode.h) and the UI
            mirrors it by declaration order (graphStore.endpointFor()).
        */
        PortPolymorphism polymorphism = PortPolymorphism::None;
    };

    /** UI-facing metadata for one parameter — enough to build a control
        without knowing the DSP. `id` follows the same stable-string rule
        as port/node type IDs.

        `isInteger` was missing entirely until a real gap it caused was
        reported: a slider for an integer-only parameter (osc.analog.shape's
        waveform index, a mock "Track" number) had no way to know it
        shouldn't accept fractional values — PortDescriptor already had
        this field, ParameterDescriptor simply never gained the matching
        one. Defaulted false so every existing positional aggregate-init
        (`{ id, min, max, default, skew, unit, displayName }`, 7 values)
        keeps compiling unchanged, same reasoning PortDescriptor's own
        trailing-defaulted-fields comment already gives.
    */
    struct ParameterDescriptor
    {
        juce::String id;
        float minValue = 0.0f;
        float maxValue = 1.0f;
        float defaultValue = 0.0f;
        float skew = 1.0f;
        juce::String unit;
        juce::String displayName;
        bool isInteger = false;

        // ---- Value contract (M14, VALUE_MODEL.md) --------------------------
        // Same fields and same reasoning as PortDescriptor's block above,
        // plus `isStructural`: VALUE_MODEL.md §5's structural-setting flag
        // belongs here, not on PortDescriptor, because a structural setting
        // is by definition never a port in the first place — it's exactly
        // what ParameterDescriptor already models (never cable-connectable,
        // always directly editable), this field just names that fact
        // explicitly instead of it being true of every ParameterDescriptor
        // by omission. Also not bindable to the host macro pool by default
        // (VALUE_MODEL.md §5) — MacroParameters/GraphEditController enforce
        // that once a macro-binding UI exists to check it against; this
        // field only carries the fact for now.
        ValueKind kind = ValueKind::Float;
        Quantity quantity = Quantity::Dimensionless;
        Curve curve = Curve::Linear;
        Polarity polarity = Polarity::Unipolar;
        std::vector<EnumOption> enumOptions;
        float step = 0.0f;
        std::optional<float> softMin;
        std::optional<float> softMax;
        bool isStructural = false;
    };
}
