// wiki/plans/LifeCycle.md 2b.4: the allocators that used to be node types
// (Voice, Trigger, Swarm) are settings of one Population now. The Add menu
// offers them as presets: choosing one places a ready-set Population (and,
// for a grain cloud, the Events that drives it) as a fragment ghost.
import type { NodeDescriptor } from './descriptorTypes'
import type { NodeFragment } from './graphStore'

interface Preset {
  id: string
  title: string
  /** What the preset is for. */
  blurb: string
  build: () => NodeFragment
}

const population = (title: string, parameters: Record<string, number>) => ({
  id: 'pop',
  type: 'life.population',
  position: { x: 0, y: 0 },
  parameters,
  properties: { title },
})

const single = (title: string, parameters: Record<string, number>): NodeFragment => ({
  nodes: [population(title, parameters)],
  connections: [],
  assets: {},
  originX: 0,
  originY: 0,
})

// Population's settings: size, born (0 input, 1 always), lives (0 held,
// 1 duration, 2 forever), whenFull (0 oldest, 1 quietest, 2 ignore).
export const PRESETS: readonly Preset[] = [
  {
    id: 'voice',
    title: 'Voice',
    blurb: 'Eight voices, one per note: wire Note In into Notes.',
    build: () => single('Voice', { 'life.population.size': 8, 'life.population.lives': 0 }),
  },
  {
    id: 'trigger',
    title: 'Trigger',
    blurb: 'One instance, restarted by every event into Born.',
    build: () => single('Trigger', { 'life.population.size': 1, 'life.population.lives': 2 }),
  },
  {
    id: 'swarmTransient',
    title: 'Swarm (Transient)',
    blurb: 'Short-lived members, one per event, each living its Duration.',
    build: () =>
      single('Swarm', { 'life.population.size': 8, 'life.population.lives': 1, 'life.population.duration': 0.1 }),
  },
  {
    id: 'swarmPopulation',
    title: 'Swarm (Population)',
    blurb: 'A fixed crowd, all alive from the start, each with its own randoms.',
    build: () => single('Swarm', { 'life.population.size': 8, 'life.population.born': 1 }),
  },
  {
    id: 'grainCloud',
    title: 'Grain Cloud',
    blurb: 'Events at 40 Hz give birth to 50 ms grains, up to 64 at once.',
    build: () => ({
      nodes: [
        { id: 'events', type: 'random.events', position: { x: -260, y: 0 }, parameters: { 'random.events.rate': 40 } },
        population('Grains', { 'life.population.size': 64, 'life.population.lives': 1, 'life.population.duration': 0.05 }),
      ],
      connections: [{ fromNodeId: 'events', fromPortId: 'event', toNodeId: 'pop', toPortId: 'born' }],
      assets: {},
      originX: 0,
      originY: 0,
    }),
  },
]

const PREFIX = 'preset:'

/** The Add menu's rows for the presets: Population's own descriptor under each preset's name. */
export function presetDescriptors(descriptors: readonly NodeDescriptor[]): NodeDescriptor[] {
  const base = descriptors.find((d) => d.typeId === 'life.population')
  if (!base) return []
  return PRESETS.map((p) => ({ ...base, typeId: PREFIX + p.id, title: p.title }))
}

/** The fragment a chosen Add-menu row stands for, or undefined for an ordinary node type. */
export function presetFragment(typeId: string): NodeFragment | undefined {
  if (!typeId.startsWith(PREFIX)) return undefined
  return PRESETS.find((p) => PREFIX + p.id === typeId)?.build()
}
