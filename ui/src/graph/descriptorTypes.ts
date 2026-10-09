// Mirrors engine/include/bazalt/engine/graph/{PortDescriptor,NodeDescriptor}.h
// and Node.h's NodeLayoutVariant field-for-field (NODE_EDITOR.md §3). Real
// descriptors arrive as JSON from the engine (fetchNodeDescriptors.ts,
// serialized by plugin/source/NodeDescriptorJson.h); mock descriptors
// (mockDescriptors.ts) are built by hand in this exact shape — one schema,
// two producers, per NODE_EDITOR.md §3's own framing.

/** How a value is carried (engine SignalType.h). A sound, a frequency, a
    modulation and a gate are all 'signal' — what one means is its `quantity`
    ('audio' for a waveform, 'boolean' for a 0/1 gate, wiki/plans/DataAndWavetable.md D1). */
export type SignalType = 'signal' | 'event' | 'note' | 'spectral' | 'data'

/** M15, SIGNAL_TYPES.md §2's Data semantic tags — mirrors engine's DataTag
    (Data.h). 'unknown' never satisfies a specific `dataTags`
    requirement; there is no wildcard match. */
export type DataTag = 'unknown' | 'curve' | 'scale' | 'wavetable' | 'modal-set' | 'sample' | 'ir'

export type NodeLayoutVariant = 'standard' | 'horizontal' | 'singleton' | 'decoration' | 'glance'

// ---- Value contract (M14, mirrors engine's PortDescriptor.h) --------------
// VALUE_MODEL.md §2/§3: one canonical description of a value, used
// identically by DSP, host automation, UI, and (eventually) macro binding.
// `kind`/`quantity`/`curve`/`polarity` default to 'float'/'dimensionless'/
// 'linear'/'unipolar' on the C++ side for every port/parameter that hasn't
// been migrated to a real quantity yet — treat those defaults as "no real
// tag set" the same way `minValue: null` already means "no bound set."
export type ValueKind = 'float' | 'int' | 'bool' | 'enum'
export type Quantity =
  | 'dimensionless'
  | 'frequency'
  | 'pitch'
  | 'time'
  | 'gain'
  | 'ratio'
  | 'unipolar'
  | 'bipolar'
  | 'count'
  | 'phase'
  | 'audio'
  | 'boolean'
export type Curve = 'linear' | 'exponential' | 'logarithmic' | 'custom-ref'
export type Polarity = 'unipolar' | 'bipolar'

export interface EnumOption {
  id: string
  label: string
}

/** SIGNAL_TYPES.md §6 — a growable port group; concrete ports are named
    `idPrefix + "0"`, `idPrefix + "1"`, ... `null` on a port that isn't part
    of a group, mirroring PortDescriptor.h's `std::optional<PortGroup>`.
*/
export interface PortGroup {
  idPrefix: string
  minCount: number
  maxCount: number
  autoRevealOnLastConnected: boolean
}

export interface PortDescriptor {
  id: string
  type: SignalType
  label: string
  isPrimaryOutput: boolean
  /** Mirrors PortDescriptor.h's own field: true only for io.output's own
      "out" port. UI-only hint — the port still fully exists for the
      compiler, this just tells the editor not to render it as a wireable
      glyph (see PortDescriptor::hidden's own comment for the full
      reasoning). NodeCard.tsx's splitPorts() filters it out entirely.
  */
  hidden: boolean
  unit: string
  minValue: number | null
  maxValue: number | null
  defaultValue: number
  isInteger: boolean
  isLogScale: boolean
  /** Mirrors PortDescriptor.h's own field, now serialized (NodeDescriptorJson.cpp):
      "can this input's value be set directly in the node, as an alternative
      to connecting a cable" — direct feedback: "we need to differentiate
      properties based on if they can be set manually or not... the dot
      literally means: I have a value, but if you want you can connect me."
      Drives NodeCard.tsx's dot-vs-arrow port-glyph state — an unconnected
      input only ever shows the dot when this is true; false means the port
      has no manual fallback at all (e.g. a plain Audio input, or a Control
      input like mix.gain's "gain" with no setParameter behind it) and always
      shows its typed glyph, connected or not. Same flag GraphCompiler
      already reads to decide the NaN-sentinel-vs-silence question — one
      concept, not two, per "the ideal implementation somehow systematically
      adds this somewhere in properties."
  */
  hasFallbackWhenUnconnected: boolean
  /** UI-only, mock-descriptor-only for now (see NodeDescriptor.isMock) —
      preset choices for a `type: 'event'` input whose fallback (see
      hasFallbackWhenUnconnected above) is a discrete pick rather than a
      numeric range (blueprint's "Random" node: Trigger is a dropdown of
      "On Note Legato"/"On Every Note"/...). Absent off the real schema
      because no real engine node routes Event through a port yet
      (CLAUDE.md's interim-simplifications note) — add this to
      PortDescriptor.h/NodeDescriptorJson.cpp for real once one does, rather
      than guessing at the shape now. The selected option is stored as its
      index into this array, reusing NodeCardState.parameterValues exactly
      like any other port's numeric fallback value — no separate state bag.
  */
  options?: string[]
  /** UI-only, mock-descriptor-only (see NodeDescriptor.isMock): the one
      named placeholder blueprint §4 calls out ("green marks polyphonic
      audio flowing into Sum Voices... poly/mono encoding will be
      redesigned later") — kept off the real schema and off
      classifyPortUiKind's SignalType-driven rules on purpose, so nothing
      generic ever reaches for it.
  */
  isPolyPlaceholder?: boolean
  /** M14 value contract, VALUE_MODEL.md §2/§3 — see this file's own header
      comment on the shared types above. */
  kind: ValueKind
  quantity: Quantity
  curve: Curve
  polarity: Polarity
  enumOptions: EnumOption[]
  step: number
  softMin: number | null
  softMax: number | null
  group: PortGroup | null
  /** M15 — meaningful only when `type === 'data'`; empty for every other
      port. See engine's PortDescriptor.h field of the same name. */
  dataTags: DataTag[]
  /** M16 — meaningful only when `type === 'audio'`. See engine's
      `Channels` enum (PortDescriptor.h) for the full rationale. */
  channels: 'mono' | 'stereo' | 'inherited'
  /** PortDescriptor.h `polymorphism`: how this port's type/quantity follow what's wired
      to its node (the declared values are only the unconnected defaults) —
      'quantity' keeps the SignalType fixed, 'signalAndQuantity' takes both.
      Absent on mock descriptors. The first-declared polymorphic input wins a
      disagreement.
  */
  polymorphism?: 'none' | 'quantity' | 'signalAndQuantity'
}

export interface ParameterDescriptor {
  id: string
  minValue: number
  maxValue: number
  defaultValue: number
  skew: number
  unit: string
  displayName: string
  /** Was missing entirely until ValueSlider's drag/type/wheel had no way to
      know a parameter shouldn't accept fractional values (e.g. a mock
      "Track" number) — PortDescriptor already had this field, this one
      just never gained the matching one. See engine's ParameterDescriptor
      struct (PortDescriptor.h) for the C++ side of this same fix.
  */
  isInteger: boolean
  /** UI-only, mock-descriptor-only for now — mirrors PortDescriptor's own
      `options` (see that field's doc comment): a discrete preset list
      instead of a continuous range, for a Macro node whose value was
      captured from a dropdown-backed port (NodeCard.tsx's ParameterRow
      renders a TriggerSelect instead of a ValueSlider when this is
      present). Not on the real C++ schema for the same reason
      PortDescriptor.options isn't: no real node has an Event-typed
      port/parameter yet.
  */
  options?: string[]
  /** M14 value contract, VALUE_MODEL.md §2/§3/§5 — see PortDescriptor's own
      header comment above for kind/quantity/curve/polarity/enumOptions/
      step/softMin/softMax. `isStructural` has no PortDescriptor
      equivalent: a structural setting is by definition never a port
      (VALUE_MODEL.md §5), so the flag only makes sense here. */
  kind: ValueKind
  quantity: Quantity
  curve: Curve
  polarity: Polarity
  enumOptions: EnumOption[]
  step: number
  softMin: number | null
  softMax: number | null
  isStructural: boolean
}

/** M20 — engine/include/bazalt/engine/graph/PreviewDescriptor.h's own
    taxonomy comment has the full rationale for which kinds are real vs.
    documented-for-later; this mirror carries all of them since the schema
    is additive regardless of which the UI currently knows how to render.
*/
export type PreviewKind =
  | 'waveform'
  | 'spectrum'
  | 'meter'
  | 'shapeWithPlayhead'
  | 'rollingHistory'
  | 'eventImpulse'
  | 'spectrogram'
  | 'goniometer'
  /** Horizontal axis is phase, not time (PreviewDescriptor.h): a generator's
      own waveform, or (foldSamples) a cable's samples folded by the upstream
      phase. Drawn by PhaseLockedPreview.tsx. */
  | 'phaseLocked'

export type ScopeTriggerMode = 'free' | 'risingEdge' | 'perNote'
export type MeterMode = 'peak' | 'rms' | 'truePeak' | 'histogram'

export interface PreviewDescriptor {
  kind: PreviewKind
  portId: string
  timeWindowSeconds: number
  triggerMode: ScopeTriggerMode
  fftSize: number
  tiltDbPerOctave: number
  averaging: number
  meterMode: MeterMode
  /** 'phaseLocked' only: fold real samples (view.cycle) rather than draw the
      phase source's own waveform. */
  foldSamples?: boolean
}

export interface NodeDescriptor {
  typeId: string
  title: string
  category: string
  icon: string
  layoutVariant: NodeLayoutVariant
  inputs: PortDescriptor[]
  outputs: PortDescriptor[]
  parameters: ParameterDescriptor[]
  /** Always a real (possibly empty) array from NodeFactory::describeAll();
      optional here only because mockDescriptors.ts's UI-only entries don't
      populate it — treat an absent value as an empty array everywhere it's
      read, same convention as isMock below.
  */
  previews?: PreviewDescriptor[]
  /** Node::hasPolymorphicPorts() (deco.reroute): the ports declared above are only
      the unconnected defaults; a placed node's real port types follow what's
      wired to it. Absent on mock descriptors (none are polymorphic). See
      graphStore.getEndpoint() for how the UI resolves them.
  */
  hasPolymorphicPorts?: boolean
  /** Node::isDeprecated(): superseded, hidden from the Add menu, but still
      loads and runs wherever an existing patch already uses it. Absent on
      mock descriptors. */
  deprecated?: boolean
  /** UI-only flag, not part of the C++ schema (NODE_EDITOR.md §3's "mock
      (UI-only) descriptors... marked as mocks") — true for every entry in
      mockDescriptors.ts, absent/false for anything NodeFactory::describeAll()
      produced. Never sent to or read from the engine.
  */
  isMock?: boolean
}
