// The Decorations family (wiki/plans/Decorations.md): canvas-only nodes that
// make the patch read like a page, not just a circuit — Header (big text),
// Comment (a paragraph), Box (a resizable, purely visual rectangle), Image (a
// decal) — plus Reroute, the one decoration that carries signal (a dot for
// cable management). None of them has the ordinary node look.
//
// Selecting and moving are the canvas's ordinary node gestures (the wrapper
// carries data-node-instance). Everything that is this card's own gesture —
// text editing, resize handles, the size/colour pickers — is marked
// `deco-own-gesture`, which InfiniteCanvas's mousedown ignores. What a
// decoration shows lives in its node properties (graphStore's
// setDecorationProperties), committed once per gesture.
import { useEffect, useRef, useState, type CSSProperties, type PointerEvent as ReactPointerEvent } from 'react'
import type { NodeDescriptor } from '../graph/descriptorTypes'
import { getSnapshot as getGraphSnapshot, setDecorationProperties, type GraphNode } from '../graph/graphStore'
import './DecorationCard.css'

/** Box tints — muted, so a box sits behind nodes without competing. */
const DECORATION_COLOURS = ['#8a8f9c', '#6fa889', '#c49a6c', '#9a86c8', '#c47a8a', '#6c9fc4']

const HEADER_SIZES = [22, 34, 52]
const DEFAULTS = {
  'deco.header': { text: 'Header' },
  'deco.comment': { text: 'Comment', width: 240 },
  'deco.box': { width: 360, height: 240 },
  'deco.image': { width: 240, height: 160 },
} as const

interface DecorationCardProps {
  node: GraphNode
  descriptor: NodeDescriptor
  selected: boolean
  connectedPortIds?: ReadonlySet<string>
}

/** Live size while a resize handle is dragged; committed on release. */
function useResize(node: GraphNode, minWidth: number, minHeight: number, keepAspect: (e: PointerEvent) => boolean) {
  const [live, setLive] = useState<{ width: number; height: number } | null>(null)

  const start = (e: ReactPointerEvent<HTMLDivElement>, startWidth: number, startHeight: number) => {
    e.preventDefault()
    e.stopPropagation()
    const wrapper = e.currentTarget.closest('.graph-node-wrapper') as HTMLElement | null
    // Screen pixels per world unit, read off the wrapper itself — no camera import needed.
    const zoom = wrapper && wrapper.offsetWidth > 0 ? wrapper.getBoundingClientRect().width / wrapper.offsetWidth : 1
    const startX = e.clientX
    const startY = e.clientY
    const aspect = startWidth / Math.max(1, startHeight)
    let latest = { width: startWidth, height: startHeight }

    const onMove = (ev: PointerEvent) => {
      let width = Math.max(minWidth, startWidth + (ev.clientX - startX) / zoom)
      let height = Math.max(minHeight, startHeight + (ev.clientY - startY) / zoom)
      if (keepAspect(ev)) {
        if (width / aspect > height) height = width / aspect
        else width = height * aspect
      }
      latest = { width: Math.round(width), height: Math.round(height) }
      setLive(latest)
    }
    const onUp = () => {
      window.removeEventListener('pointermove', onMove)
      window.removeEventListener('pointerup', onUp)
      setDecorationProperties(node.id, latest)
      // The committed value replaces the live one once the store has it.
      setTimeout(() => setLive(null), 0)
    }
    window.addEventListener('pointermove', onMove)
    window.addEventListener('pointerup', onUp)
  }
  return { live, start }
}

/** Double-click-to-edit text, committed on Enter (Shift+Enter: new line),
    blur, or cancelled with Escape. */
function EditableText({ value, placeholder, className, style, multiline, onCommit }: {
  value: string | undefined
  placeholder: string
  className: string
  style?: CSSProperties
  multiline: boolean
  onCommit: (text: string) => void
}) {
  const [editing, setEditing] = useState(false)
  const resolved = useRef(false)
  const textRef = useRef<HTMLTextAreaElement | null>(null)

  useEffect(() => {
    if (editing && textRef.current) {
      textRef.current.focus()
      textRef.current.select()
    }
  }, [editing])

  const finish = (commit: boolean) => {
    if (resolved.current) return
    resolved.current = true
    if (commit && textRef.current && textRef.current.value !== (value ?? '')) onCommit(textRef.current.value)
    setEditing(false)
  }

  if (editing)
    return (
      <textarea
        ref={textRef}
        className={`${className} deco-text-input deco-own-gesture`}
        style={style}
        defaultValue={value ?? ''}
        rows={multiline ? Math.max(2, (value ?? '').split('\n').length) : 1}
        onBlur={() => finish(true)}
        onKeyDown={(e) => {
          e.stopPropagation()
          if (e.key === 'Escape') finish(false)
          else if (e.key === 'Enter' && !e.shiftKey) {
            e.preventDefault()
            finish(true)
          }
        }}
      />
    )

  return (
    <div
      className={`${className}${value ? '' : ' deco-placeholder'}`}
      style={style}
      onDoubleClick={(e) => {
        e.stopPropagation()
        resolved.current = false
        setEditing(true)
      }}
    >
      {value || placeholder}
    </div>
  )
}

export function DecorationCard({ node, descriptor, selected, connectedPortIds }: DecorationCardProps) {
  const deco = node.decoration ?? {}
  const selectedClass = selected ? ' deco-selected' : ''

  if (node.typeId === 'deco.reroute') return <RerouteDot node={node} descriptor={descriptor} selected={selected} connectedPortIds={connectedPortIds} />

  if (node.typeId === 'deco.header') {
    const size = deco.size ?? 1
    return (
      <div className={`deco deco-header${selectedClass}`}>
        <EditableText
          value={deco.text}
          placeholder={DEFAULTS['deco.header'].text}
          className="deco-header-text"
          style={{ fontSize: HEADER_SIZES[size] ?? HEADER_SIZES[1] }}
          multiline={false}
          onCommit={(text) => setDecorationProperties(node.id, { text })}
        />
        {selected && (
          <div className="deco-toolbar deco-own-gesture">
            {['S', 'M', 'L'].map((label, index) => (
              <button key={label} className={index === size ? 'deco-chip deco-chip-active' : 'deco-chip'} onClick={() => setDecorationProperties(node.id, { size: index })}>
                {label}
              </button>
            ))}
          </div>
        )}
      </div>
    )
  }

  if (node.typeId === 'deco.comment') return <CommentCard node={node} selected={selected} />
  if (node.typeId === 'deco.box') return <BoxCard node={node} selected={selected} />
  if (node.typeId === 'deco.image') return <ImageCard node={node} selected={selected} />
  return null
}

function CommentCard({ node, selected }: { node: GraphNode; selected: boolean }) {
  const deco = node.decoration ?? {}
  const width = deco.width ?? DEFAULTS['deco.comment'].width
  const resize = useResize(node, 120, 0, () => false)
  const shownWidth = resize.live?.width ?? width
  return (
    <div className={`deco deco-comment${selected ? ' deco-selected' : ''}`} style={{ width: shownWidth }}>
      <EditableText
        value={deco.text}
        placeholder={DEFAULTS['deco.comment'].text}
        className="deco-comment-text"
        multiline
        onCommit={(text) => setDecorationProperties(node.id, { text })}
      />
      {selected && <div className="deco-resize deco-resize-side deco-own-gesture" onPointerDown={(e) => resize.start(e, width, 0)} />}
    </div>
  )
}

function BoxCard({ node, selected }: { node: GraphNode; selected: boolean }) {
  const deco = node.decoration ?? {}
  const width = deco.width ?? DEFAULTS['deco.box'].width
  const height = deco.height ?? DEFAULTS['deco.box'].height
  const colour = DECORATION_COLOURS[deco.colour ?? 0] ?? DECORATION_COLOURS[0]
  const resize = useResize(node, 60, 40, () => false)
  const size = resize.live ?? { width, height }
  return (
    <div
      className={`deco deco-box${selected ? ' deco-selected' : ''}`}
      style={{ width: size.width, height: size.height, '--deco-colour': colour } as CSSProperties}
    >
      <EditableText value={deco.text} placeholder={selected ? 'Label' : ''} className="deco-box-label" multiline={false} onCommit={(text) => setDecorationProperties(node.id, { text })} />
      {selected && (
        <>
          <div className="deco-toolbar deco-own-gesture">
            {DECORATION_COLOURS.map((c, index) => (
              <button
                key={c}
                className={index === (deco.colour ?? 0) ? 'deco-swatch deco-swatch-active' : 'deco-swatch'}
                style={{ background: c }}
                aria-label={`Colour ${index + 1}`}
                onClick={() => setDecorationProperties(node.id, { colour: index })}
              />
            ))}
          </div>
          <div className="deco-resize deco-resize-corner deco-own-gesture" onPointerDown={(e) => resize.start(e, width, height)} />
        </>
      )}
    </div>
  )
}

function ImageCard({ node, selected }: { node: GraphNode; selected: boolean }) {
  const deco = node.decoration ?? {}
  const width = deco.width ?? DEFAULTS['deco.image'].width
  const height = deco.height ?? DEFAULTS['deco.image'].height
  // Aspect locked; Shift frees it.
  const resize = useResize(node, 24, 24, (e) => !e.shiftKey)
  const size = resize.live ?? { width, height }
  const asset = deco.asset ? getGraphSnapshot().assets.get(deco.asset) : undefined
  return (
    <div className={`deco deco-image${selected ? ' deco-selected' : ''}`} style={{ width: size.width, height: size.height }}>
      {asset ? (
        <img className="deco-image-img" src={`data:${asset.type};base64,${asset.data}`} alt="" draggable={false} />
      ) : (
        <div className="deco-image-missing">Image</div>
      )}
      {selected && <div className="deco-resize deco-resize-corner deco-own-gesture" onPointerDown={(e) => resize.start(e, width, height)} />}
    </div>
  )
}

/** A dot for cable management: drag it to move it, drop a cable onto it to
    feed it, drag a new cable out of the thin zone just past its right edge. */
function RerouteDot({ node, descriptor, selected, connectedPortIds }: DecorationCardProps) {
  const input = descriptor.inputs[0]
  const output = descriptor.outputs[0]
  const connected = (connectedPortIds?.size ?? 0) > 0
  return (
    <div className={`deco-reroute${selected ? ' deco-selected' : ''}${connected ? ' deco-reroute-connected' : ''}`}>
      {input && <span className="deco-reroute-anchor deco-reroute-in" data-node-id={node.id} data-port-id={input.id} data-direction="input" data-port-anchor="" />}
      {output && <span className="deco-reroute-anchor deco-reroute-out" data-node-id={node.id} data-port-id={output.id} data-direction="output" data-port-anchor="" />}
    </div>
  )
}
