// Mirrors engine/include/bazalt/engine/graph/{PortDescriptor,NodeDescriptor}.h
// and Node.h's NodeLayoutVariant field-for-field (NODE_EDITOR.md §3). Real
// descriptors arrive as JSON from the engine (fetchNodeDescriptors.ts,
// serialized by plugin/source/NodeDescriptorJson.h); mock descriptors
// (mockDescriptors.ts) are built by hand in this exact shape — one schema,
// two producers, per NODE_EDITOR.md §3's own framing.

export type SignalType = 'audio' | 'control' | 'event' | 'note' | 'spectral' | 'boolean'

export type NodeLayoutVariant = 'standard' | 'horizontal' | 'singleton' | 'decoration'

export interface PortDescriptor {
  id: string
  type: SignalType
  label: string
  isPrimaryOutput: boolean
  unit: string
  minValue: number | null
  maxValue: number | null
  defaultValue: number
  isInteger: boolean
  isLogScale: boolean
  /** UI-only, mock-descriptor-only (see NodeDescriptor.isMock): the one
      named placeholder blueprint §4 calls out ("green marks polyphonic
      audio flowing into Sum Voices... poly/mono encoding will be
      redesigned later") — kept off the real schema and off
      classifyPortUiKind's SignalType-driven rules on purpose, so nothing
      generic ever reaches for it.
  */
  isPolyPlaceholder?: boolean
}

export interface ParameterDescriptor {
  id: string
  minValue: number
  maxValue: number
  defaultValue: number
  skew: number
  unit: string
  displayName: string
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
  /** UI-only flag, not part of the C++ schema (NODE_EDITOR.md §3's "mock
      (UI-only) descriptors... marked as mocks") — true for every entry in
      mockDescriptors.ts, absent/false for anything NodeFactory::describeAll()
      produced. Never sent to or read from the engine.
  */
  isMock?: boolean
}
