// Synthetic node/cable layout for the M8 rendering-performance spike —
// deliberately NOT the real engine graph (see StressGraphGenerator.h on
// the C++ side for that, engine-scalability test). This one only needs to
// look plausible and hit the target counts; NODE_EDITOR.md §3's 120fps/
// 500-node/1000-cable target is a rendering question, independent of what
// the nodes actually compute.

export interface StressNode {
  id: number
  x: number
  y: number
}

export interface StressCable {
  fromNode: number
  toNode: number
}

export interface StressGraph {
  nodes: StressNode[]
  cables: StressCable[]
}

const NODE_SPACING = 160
const COLUMNS = 25

function mulberry32(seed: number) {
  let a = seed
  return () => {
    a |= 0
    a = (a + 0x6d2b79f5) | 0
    let t = Math.imul(a ^ (a >>> 15), 1 | a)
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296
  }
}

export function generateStressGraph(numNodes: number, numCables: number): StressGraph {
  const random = mulberry32(1234)
  const nodes: StressNode[] = []

  for (let i = 0; i < numNodes; i++) {
    const col = i % COLUMNS
    const row = Math.floor(i / COLUMNS)
    nodes.push({
      id: i,
      x: col * NODE_SPACING + (random() - 0.5) * 40,
      y: row * NODE_SPACING + (random() - 0.5) * 40,
    })
  }

  const cables: StressCable[] = []

  // Snake chain first (numNodes - 1 cables) so the graph reads as
  // connected, not just noise, then fill the rest with shorter-range
  // random pairs for visual density.
  for (let i = 0; i < numNodes - 1 && cables.length < numCables; i++) {
    cables.push({ fromNode: i, toNode: i + 1 })
  }

  while (cables.length < numCables) {
    const from = Math.floor(random() * numNodes)
    const spread = Math.min(numNodes - 1, 40)
    const offset = 1 + Math.floor(random() * spread)
    const to = (from + offset) % numNodes
    if (from !== to) cables.push({ fromNode: from, toNode: to })
  }

  return { nodes, cables }
}
