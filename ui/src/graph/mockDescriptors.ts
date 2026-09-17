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
// (util.reroute) and no Horizontal or Singleton node at all yet.
//
// Chosen to mirror docs/Frame 1 Bazalt.png's own example nodes directly
// where possible (MIDI Note, Trigger by Threshold, Predelay, Random, Macro
// 1, Sum Voices/Master Out/Singleton, Bass frame) so the gallery doubles as
// a literal side-by-side check against the reference.
import type { NodeDescriptor, PortDescriptor } from './descriptorTypes'

function port(p: Partial<PortDescriptor> & Pick<PortDescriptor, 'id' | 'type' | 'label'>): PortDescriptor {
  return {
    isPrimaryOutput: false,
    unit: '',
    minValue: null,
    maxValue: null,
    defaultValue: 0,
    isInteger: false,
    isLogScale: false,
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
    parameters: [{ id: 'track', minValue: 1, maxValue: 16, defaultValue: 1, skew: 1, unit: '', displayName: 'Track' }],
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
    inputs: [port({ id: 'audio', type: 'audio', label: 'Audio' })],
    outputs: [port({ id: 'audio', type: 'audio', label: 'Audio', isPrimaryOutput: true })],
    parameters: [{ id: 'byMs', minValue: 0, maxValue: 500, defaultValue: 34, skew: 1, unit: 'ms', displayName: 'By' }],
  },
  {
    typeId: 'mock.triggerByThreshold',
    title: 'Trigger by Threshold',
    category: 'Utility',
    icon: 'threshold',
    layoutVariant: 'standard',
    isMock: true,
    inputs: [port({ id: 'by', type: 'control', label: 'By', unit: '' })],
    outputs: [port({ id: 'onThreshold', type: 'event', label: 'On Threshold', isPrimaryOutput: true })],
    parameters: [{ id: 'threshold', minValue: 0, maxValue: 100, defaultValue: 50, skew: 1, unit: '%', displayName: 'Threshold' }],
  },
  {
    typeId: 'mock.random',
    title: 'Random',
    category: 'Modulation',
    icon: 'random',
    layoutVariant: 'horizontal',
    isMock: true,
    inputs: [port({ id: 'trigger', type: 'event', label: 'Trigger' })],
    outputs: [port({ id: 'out', type: 'control', label: 'Out', isPrimaryOutput: true })],
    parameters: [{ id: 'rate', minValue: 0.01, maxValue: 20, defaultValue: 3, skew: 0.4, unit: 'Hz', displayName: 'Rate' }],
  },
  {
    typeId: 'mock.macro',
    title: 'Macro 1',
    category: 'Macro',
    icon: 'macro',
    layoutVariant: 'standard',
    isMock: true,
    inputs: [],
    outputs: [],
    parameters: [{ id: 'value', minValue: 0, maxValue: 100, defaultValue: 42.53, skew: 1, unit: '%', displayName: 'Macro 1' }],
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
