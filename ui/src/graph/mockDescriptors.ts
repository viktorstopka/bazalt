// UI-only mock node descriptors (NODE_EDITOR.md §3, blueprint §1: "Where
// the UI needs such nodes to demonstrate or test something, use UI-only
// mock descriptors clearly marked as mocks"). Every entry here has
// isMock: true and a typeId under a namespace no real NodeFactory
// registration will ever use ("mock.*", plus "deco.*" for the three
// decoration kinds NODE_EDITOR.md §4 names but MILESTONES.md defers the
// real engine implementation of to M12). These exist purely so the M9
// component gallery has something to render for every layout variant and
// interaction the blueprint's design reference shows, since the real
// engine registry (16 types as of M8) has only one Decoration node
// (deco.reroute) and no Horizontal or Singleton node at all yet.
//
// Chosen to mirror docs/Frame 1 Bazalt.png's own example nodes directly
// where possible (MIDI Note, Trigger by Threshold, Predelay, Random, Macro
// 1, Sum Voices/Master Out/Singleton, Bass frame) so the gallery doubles as
// a literal side-by-side check against the reference.
import type { NodeDescriptor, ParameterDescriptor, PortDescriptor } from './descriptorTypes'

// M14 value-contract defaults, shared by port()/parameter() below — matches
// PortDescriptor.h/ParameterDescriptor's own C++ defaults exactly (Float
// kind, Dimensionless quantity, Linear curve, Unipolar polarity, no enum
// options, continuous step, no soft range) so a mock descriptor that
// doesn't set one of these fields behaves identically to a real, unmigrated
// engine port/parameter — same "no real tag set" meaning as `minValue: null`.
const VALUE_CONTRACT_DEFAULTS = {
  kind: 'float' as const,
  quantity: 'dimensionless' as const,
  curve: 'linear' as const,
  polarity: 'unipolar' as const,
  enumOptions: [],
  step: 0,
  softMin: null,
  softMax: null,
}

function port(p: Partial<PortDescriptor> & Pick<PortDescriptor, 'id' | 'type' | 'label'>): PortDescriptor {
  return {
    isPrimaryOutput: false,
    hidden: false,
    unit: '',
    minValue: null,
    maxValue: null,
    defaultValue: 0,
    isInteger: false,
    isLogScale: false,
    // Default false, matching PortDescriptor.h's own default: a port only
    // gets the dot-when-unconnected state (NodeCard.tsx) when explicitly
    // marked as having a real in-node fallback — see descriptorTypes.ts's
    // doc comment on this field.
    hasFallbackWhenUnconnected: false,
    ...VALUE_CONTRACT_DEFAULTS,
    group: null,
    dataTags: [],
    channels: 'mono',
    ...p,
  }
}

function parameter(p: Partial<ParameterDescriptor> & Pick<ParameterDescriptor, 'id'>): ParameterDescriptor {
  return {
    minValue: 0,
    maxValue: 1,
    defaultValue: 0,
    skew: 1,
    unit: '',
    displayName: '',
    isInteger: false,
    ...VALUE_CONTRACT_DEFAULTS,
    isStructural: false,
    ...p,
  }
}

export const MOCK_DESCRIPTORS: NodeDescriptor[] = [
  {
    typeId: 'mock.midiNote',
    title: 'MIDI Note',
    category: 'Source',
    icon: 'note',
    layoutVariant: 'standard',
    isMock: true,
    inputs: [],
    outputs: [
      port({ id: 'legato', type: 'event', label: 'Legato' }),
      port({ id: 'noteOn', type: 'boolean', label: 'Note On' }),
      port({ id: 'pitch', type: 'control', label: 'Pitch', unit: 'st', isPrimaryOutput: true }),
      port({ id: 'velocity', type: 'control', label: 'Velocity', unit: '%' }),
    ],
    parameters: [],
  },
  {
    typeId: 'mock.midiCc',
    title: 'MIDI CC',
    category: 'Source',
    icon: 'cc',
    layoutVariant: 'standard',
    isMock: true,
    inputs: [],
    outputs: [port({ id: 'value', type: 'control', label: 'Bind', unit: '%', isPrimaryOutput: true })],
    parameters: [],
  },
  {
    typeId: 'mock.audioIn',
    title: 'Audio In',
    category: 'Source',
    icon: 'audioIn',
    layoutVariant: 'standard',
    isMock: true,
    inputs: [],
    outputs: [port({ id: 'out', type: 'audio', label: 'OUT', isPrimaryOutput: true })],
    parameters: [parameter({ id: 'track', minValue: 1, maxValue: 16, defaultValue: 1, displayName: 'Track', isInteger: true })],
  },
  {
    // The reference's own error-state example (a filled red circle + "!"
    // beside the title, blueprint §4) — reused here as the gallery's error
    // state demo rather than inventing a second one.
    typeId: 'mock.predelay',
    title: 'Predelay',
    category: 'Effects',
    icon: 'predelay',
    layoutVariant: 'standard',
    isMock: true,
    // "audio" stays first (splitPorts()' primary-input candidate for the
    // merged pass-through row) — "by" is appended after it as an ordinary,
    // separately-connectable Control input, not merged. Modulating a delay
    // time is exactly the kind of thing this system exists for (direct
    // feedback) — no reason this should be a non-connectable parameter.
    inputs: [
      port({ id: 'audio', type: 'audio', label: 'Audio' }),
      port({ id: 'byMs', type: 'control', label: 'By', unit: 'ms', minValue: 0, maxValue: 500, defaultValue: 34, isInteger: true, hasFallbackWhenUnconnected: true }),
    ],
    outputs: [port({ id: 'audio', type: 'audio', label: 'Audio', isPrimaryOutput: true })],
    parameters: [],
  },
  {
    typeId: 'mock.triggerByThreshold',
    title: 'Trigger by Threshold',
    category: 'Utility',
    icon: 'threshold',
    layoutVariant: 'standard',
    isMock: true,
    inputs: [
      port({ id: 'by', type: 'control', label: 'By', unit: '' }),
      port({ id: 'threshold', type: 'control', label: 'Threshold', unit: '%', minValue: 0, maxValue: 100, defaultValue: 50, hasFallbackWhenUnconnected: true }),
    ],
    outputs: [port({ id: 'onThreshold', type: 'event', label: 'On Threshold', isPrimaryOutput: true })],
    parameters: [],
  },
  {
    typeId: 'mock.random',
    title: 'Random',
    category: 'Modulation',
    icon: 'random',
    layoutVariant: 'horizontal',
    isMock: true,
    inputs: [
      // Trigger has no numeric range at all — its in-node fallback (per
      // direct feedback: "you can have a dropdown... the example is on the
      // Random node... Trigger property is selectable in a dropdown") is a
      // discrete preset list instead, which is what `options` + the event
      // SignalType together signal to PortRow (NodeCard.tsx renders
      // TriggerSelect instead of ValueSlider for that combination). Index 0
      // ("On Every Note") is the default preset.
      port({
        id: 'trigger',
        type: 'event',
        label: 'Trigger',
        hasFallbackWhenUnconnected: true,
        options: ['On Every Note', 'On Note Legato', 'On Note On', 'Manual'],
        defaultValue: 0,
      }),
      port({ id: 'rate', type: 'control', label: 'Rate', unit: 'Hz', minValue: 0.01, maxValue: 20, defaultValue: 3, isLogScale: true, hasFallbackWhenUnconnected: true }),
    ],
    outputs: [port({ id: 'out', type: 'control', label: 'Out', isPrimaryOutput: true })],
    parameters: [],
  },
  {
    typeId: 'mock.macro',
    title: 'Macro 1',
    category: 'Macro',
    icon: 'macro',
    layoutVariant: 'standard',
    isMock: true,
    inputs: [],
    // A real, connectable output (added for the "drag an unconnected input
    // out to empty space -> new Macro" shortcut, graphStore.ts's
    // addMacroFromPort) — every macro's actual shape (type/range/unit/
    // options) is a per-instance override on the GraphNode itself
    // (GraphNode.macroConfig, resolved by resolveNodeDescriptor), this base
    // entry is just the shape a manually-placed, unconfigured Macro gets.
    outputs: [port({ id: 'out', type: 'control', label: 'Out', unit: '%', minValue: 0, maxValue: 100, defaultValue: 42.53, isPrimaryOutput: true })],
    parameters: [parameter({ id: 'value', minValue: 0, maxValue: 100, defaultValue: 42.53, unit: '%', displayName: 'Macro 1' })],
  },
  {
    typeId: 'mock.sumVoices',
    title: 'Sum Voices',
    category: 'Utility',
    icon: 'singleton',
    layoutVariant: 'singleton',
    isMock: true,
    inputs: [port({ id: 'in', type: 'audio', label: '', isPolyPlaceholder: true })],
    outputs: [port({ id: 'out', type: 'audio', label: '', isPrimaryOutput: true })],
    parameters: [],
  },
  {
    typeId: 'mock.masterOut',
    title: 'Master Out',
    category: 'Utility',
    icon: 'singleton',
    layoutVariant: 'singleton',
    isMock: true,
    inputs: [port({ id: 'in', type: 'audio', label: '' })],
    outputs: [],
    parameters: [],
  },
  {
    typeId: 'mock.singleton1',
    title: 'Singleton 1',
    category: 'Utility',
    icon: 'singleton',
    layoutVariant: 'singleton',
    isMock: true,
    inputs: [port({ id: 'in', type: 'audio', label: '' })],
    outputs: [port({ id: 'out', type: 'audio', label: '', isPrimaryOutput: true })],
    parameters: [],
  },
  {
    typeId: 'mock.singleton2',
    title: 'Singleton 2',
    category: 'Utility',
    icon: 'singleton',
    layoutVariant: 'singleton',
    isMock: true,
    inputs: [port({ id: 'in', type: 'audio', label: '' })],
    outputs: [port({ id: 'out', type: 'audio', label: '', isPrimaryOutput: true })],
    parameters: [],
  },
  {
    typeId: 'deco.frame',
    title: 'Bass',
    category: 'Decoration',
    icon: 'frame',
    layoutVariant: 'decoration',
    isMock: true,
    inputs: [],
    outputs: [],
    parameters: [],
  },
  {
    typeId: 'deco.header',
    title: 'Header label',
    category: 'Decoration',
    icon: 'header',
    layoutVariant: 'decoration',
    isMock: true,
    inputs: [],
    outputs: [],
    parameters: [],
  },
  {
    typeId: 'deco.image',
    title: 'Image',
    category: 'Decoration',
    icon: 'image',
    layoutVariant: 'decoration',
    isMock: true,
    inputs: [],
    outputs: [],
    parameters: [],
  },
]
