// Dev-only component gallery (M9, NODE_EDITOR.md §3 / blueprint §8
// deliverable #2): "every node layout variant (standard/horizontal/
// singleton/decoration), every port type/glyph, and every control state...
// from descriptors alone — no live graph yet." Renders NodeCard purely from
// descriptors (real, fetched once from the engine over the M7 command
// bridge, plus UI-only mocks) — nothing here hardcodes a node type; adding
// a new engine node type or a new mock makes it show up automatically.
import { useEffect, useState } from 'react'
import { NodeCard } from '../nodes/NodeCard'
import { fetchNodeDescriptors } from '../graph/fetchNodeDescriptors'
import { MOCK_DESCRIPTORS } from '../graph/mockDescriptors'
import { PORT_UI_STYLE, portUiStyle, type PortUiKind } from '../graph/portUiKind'
import type { NodeDescriptor, PortDescriptor } from '../graph/descriptorTypes'
import { tokens } from '../theme/tokens'
import './ComponentGallery.css'

const PORT_KIND_ORDER: PortUiKind[] = ['audio', 'modulation', 'value', 'integer', 'trigger', 'boolean']

function Section({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <section className="gallery-section">
      <h2 className="gallery-section-title">{title}</h2>
      <div className="gallery-section-body">{children}</div>
    </section>
  )
}

function LegendSwatch({ kind }: { kind: PortUiKind }) {
  const style = PORT_UI_STYLE[kind]
  return (
    <div className="gallery-legend-swatch">
      <span className="gallery-legend-glyph" style={{ color: style.color }}>
        {style.glyph}
      </span>
      <span className="gallery-legend-label">{kind}</span>
      <span className="gallery-legend-color">{style.color}</span>
    </div>
  )
}

function ChainGlyph({ port }: { port: PortDescriptor }) {
  const style = portUiStyle(port)
  const color = port.isPolyPlaceholder ? tokens.color.portPoly : style.color
  return (
    <span className="chain-glyph" style={{ color }}>
      {style.glyph}
    </span>
  )
}

/** The "adjacent singletons auto-merge into a chain" demo (blueprint §4:
    "frames touch, arrows join") — deliberately NOT built out of ordinary
    standalone SingletonBody instances (each of which correctly draws its
    OWN input/output arrow — see the "Master Out" catalog entry, one arrow,
    since it has no output port). Placed edge to edge, that would draw TWO
    arrows at every junction (box A's own output arrow, then box B's own
    input arrow) where the reference shows exactly one flowing through the
    seam. So this renders one arrow per junction instead, sourced from
    whichever side of that junction actually has a port (a sink like
    Master Out contributes none, so the neighbour's port colours it), plus
    a single stub arrow at each true end of the chain. The real "auto-merge"
    interaction (snap/pull-out/insert-between) is still M12's job — this is
    only the resting visual.
*/
function SingletonChainDemo({ nodes }: { nodes: NodeDescriptor[] }) {
  return (
    <div className="gallery-singleton-chain">
      {nodes.map((node, i) => {
        const leadingPort = i === 0 ? node.inputs[0] : undefined
        const trailingPort = i === nodes.length - 1 ? node.outputs[0] : (node.outputs[0] ?? nodes[i + 1]?.inputs[0])
        return (
          <div key={node.typeId} className="chain-segment">
            {leadingPort && <ChainGlyph port={leadingPort} />}
            <span className="chain-node">{node.title}</span>
            {trailingPort && <ChainGlyph port={trailingPort} />}
          </div>
        )
      })}
    </div>
  )
}

function CatalogEntry({ descriptor }: { descriptor: NodeDescriptor }) {
  return (
    <div className="gallery-catalog-entry">
      <NodeCard descriptor={descriptor} />
      <div className="gallery-catalog-caption">
        <span>{descriptor.typeId}</span>
        {descriptor.isMock && <span className="gallery-mock-tag">mock</span>}
      </div>
    </div>
  )
}

interface ComponentGalleryProps {
  onClose: () => void
}

export function ComponentGallery({ onClose }: ComponentGalleryProps) {
  const [realDescriptors, setRealDescriptors] = useState<NodeDescriptor[]>([])
  const [loaded, setLoaded] = useState(false)

  useEffect(() => {
    let cancelled = false
    fetchNodeDescriptors().then((descriptors) => {
      if (!cancelled) {
        setRealDescriptors(descriptors)
        setLoaded(true)
      }
    })
    return () => {
      cancelled = true
    }
  }, [])

  const allDescriptors = [...realDescriptors, ...MOCK_DESCRIPTORS]
  const byCategory = new Map<string, NodeDescriptor[]>()
  for (const d of allDescriptors) {
    const list = byCategory.get(d.category) ?? []
    list.push(d)
    byCategory.set(d.category, list)
  }

  const standardExample = realDescriptors.find((d) => d.layoutVariant === 'standard') ?? MOCK_DESCRIPTORS.find((d) => d.typeId === 'mock.triggerByThreshold')!
  const singletonChain = MOCK_DESCRIPTORS.filter((d) => d.layoutVariant === 'singleton')
  const horizontalExample = MOCK_DESCRIPTORS.find((d) => d.layoutVariant === 'horizontal')!
  const decorationExamples = allDescriptors.filter((d) => d.layoutVariant === 'decoration')

  return (
    <div className="component-gallery">
      <div className="gallery-topbar">
        <span className="gallery-title">Component gallery (M9)</span>
        <span className="gallery-status">
          {loaded ? `${realDescriptors.length} real + ${MOCK_DESCRIPTORS.length} mock descriptors` : 'loading real descriptors…'}
        </span>
        <button className="gallery-close" onClick={onClose}>
          Close
        </button>
      </div>

      <div className="gallery-scroll">
        <Section title="Layout variants">
          <div className="gallery-row">
            <div className="gallery-variant-cell">
              <NodeCard descriptor={standardExample} />
              <span className="gallery-variant-label">standard</span>
            </div>
            <div className="gallery-variant-cell">
              <NodeCard descriptor={horizontalExample} />
              <span className="gallery-variant-label">horizontal</span>
            </div>
            <div className="gallery-variant-cell">
              <SingletonChainDemo nodes={singletonChain} />
              <span className="gallery-variant-label">singleton (auto-merge chain, M12)</span>
            </div>
            <div className="gallery-variant-cell">
              <div className="gallery-row">
                {decorationExamples.map((d) => (
                  <NodeCard key={d.typeId} descriptor={d} />
                ))}
              </div>
              <span className="gallery-variant-label">decoration</span>
            </div>
          </div>
        </Section>

        <Section title="Port types &amp; glyphs (NODE_EDITOR.md §5)">
          <div className="gallery-legend">
            {PORT_KIND_ORDER.map((kind) => (
              <LegendSwatch key={kind} kind={kind} />
            ))}
            <div className="gallery-legend-swatch">
              <span className="gallery-legend-glyph" style={{ color: tokens.color.portPoly }}>
                {'→'}
              </span>
              <span className="gallery-legend-label">poly (placeholder)</span>
              <span className="gallery-legend-color">{tokens.color.portPoly}</span>
            </div>
          </div>
        </Section>

        <Section title="Node states (blueprint §8: default/selected/connected/bypassed/listening/error — no hover, per feedback)">
          <div className="gallery-row">
            <div className="gallery-variant-cell">
              <NodeCard descriptor={standardExample} />
              <span className="gallery-variant-label">default</span>
            </div>
            <div className="gallery-variant-cell">
              <NodeCard descriptor={standardExample} state={{ selected: true }} />
              <span className="gallery-variant-label">selected</span>
            </div>
            <div className="gallery-variant-cell">
              <NodeCard
                descriptor={standardExample}
                state={{ connectedPortIds: new Set(standardExample.outputs.map((o) => o.id)), demoConnectedValue: '0.42' }}
              />
              <span className="gallery-variant-label">connected</span>
            </div>
            <div className="gallery-variant-cell">
              <NodeCard descriptor={standardExample} state={{ bypassed: true }} />
              <span className="gallery-variant-label">bypassed</span>
            </div>
            <div className="gallery-variant-cell">
              <NodeCard descriptor={standardExample} state={{ listening: true }} />
              <span className="gallery-variant-label">listening</span>
            </div>
            <div className="gallery-variant-cell">
              <NodeCard descriptor={MOCK_DESCRIPTORS.find((d) => d.typeId === 'mock.predelay')!} state={{ error: 'Feedback path exceeds one block of delay' }} />
              <span className="gallery-variant-label">error</span>
            </div>
          </div>
        </Section>

        <Section title="Macro node (blueprint §4: dotted outline, info button, value slider)">
          <div className="gallery-row">
            <NodeCard descriptor={MOCK_DESCRIPTORS.find((d) => d.typeId === 'mock.macro')!} />
          </div>
        </Section>

        <Section title={`Full catalog (${allDescriptors.length} descriptors)`}>
          {[...byCategory.entries()].map(([category, descriptors]) => (
            <div key={category} className="gallery-category">
              <h3 className="gallery-category-title">{category}</h3>
              <div className="gallery-row">
                {descriptors.map((d) => (
                  <CatalogEntry key={d.typeId} descriptor={d} />
                ))}
              </div>
            </div>
          ))}
        </Section>
      </div>
    </div>
  )
}
