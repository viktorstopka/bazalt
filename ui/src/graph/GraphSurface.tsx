// The DOM "world layer" for the M10 node editor: one absolutely-positioned
// wrapper per graphStore node, in world (unscaled) units — InfiniteCanvas.tsx
// applies the pan/zoom CSS transform to this layer's parent container once
// per animation frame, imperatively, so nothing here needs to know about
// the camera at all (see nodeEditorRenderer.ts's header comment for the
// full "DOM is the layout source of truth" rationale).
//
// Handles the node-local interactions that don't need camera/selection
// coordination — bypass-icon click, double-click-to-rename, right-click
// context menu — directly against graphStore, stopping propagation so
// InfiniteCanvas's centralized pointer handlers never see those clicks.
// Everything else (plain body click/drag, port drag, empty-canvas
// click/drag) deliberately does NOT stop propagation, so it bubbles up to
// InfiniteCanvas's own listeners, which inspect the DOM target directly
// (via data-node-instance/data-port-anchor) rather than going through
// callback props — see GraphSurface's sibling components for that half.
import { useEffect, useMemo, useRef, useState, type RefObject } from 'react'
import { createPortal } from 'react-dom'
import { NodeCard, type NodeCardState } from '../nodes/NodeCard'
import { DecorationCard } from '../nodes/DecorationCard'
import { NodeContextMenu } from './NodeContextMenu'
import {
  renameNode,
  toggleBypass,
  toggleBypassMany,
  setAsOutput,
  deleteNodes,
  setSelection,
  setParameterValue,
  setMacroEnumOptionLabels,
  setCountMin,
  setCountMax,
  setViewerRangeMin,
  setViewerRangeMax,
  setViewerCenter,
  setPreviewPlayheadMode,
  setParameterLive,
  resolveNodeDescriptor,
  DECORATION_TYPES,
  BACKGROUND_DECORATIONS,
  type GraphNode,
  type GraphWire,
  type NodeMultiplicity,
  fragmentPreviewNodes,
} from './graphStore'
import type { NodeDescriptor } from './descriptorTypes'
import type { GhostPlacement } from '../canvas/interactionStore'
import { getTitleGeometry } from './titleGeometry'
import './GraphSurface.css'

interface NodeWrapperProps {
  node: GraphNode
  descriptor: NodeDescriptor
  selected: boolean
  selection: ReadonlySet<string>
  connectedPortIds: ReadonlySet<string> | undefined
  multiplicity: NodeMultiplicity | undefined
  /** This node runs once per instance: drawn as a stack of cards (null: it runs once). */
  stack: InstanceStack | null
  overlayTarget: HTMLElement | null
  ghostActive: boolean
  /** A Listen is fed from this node — it is what you hear. */
  listening: boolean
}

/** wiki/plans/DataAndWavetable.md D4: a node that runs once per instance is
    drawn with offset outline copies behind it and a ×N badge — local to the
    node, so moving it anywhere changes nothing, and cables stay clean. `side`
    marks a boundary node: Merge (was Voice Sum) has the stack on its input
    side only, the allocator on its output side only. */
export interface InstanceStack {
  count: number
  side: 'both' | 'in' | 'out'
}

export function instanceStackFor(
  descriptor: NodeDescriptor,
  own: NodeMultiplicity | undefined,
  all: ReadonlyMap<string, NodeMultiplicity>,
): InstanceStack | null {
  if (!own) return null
  const polyOrigin = (ids: readonly string[]) => {
    for (const id of ids) {
      const info = own.ports.get(id)
      if (info?.kind === 'poly') return info.originId ?? ''
    }
    return undefined
  }
  const inOrigin = polyOrigin(descriptor.inputs.map((p) => p.id))
  const outOrigin = polyOrigin(descriptor.outputs.map((p) => p.id))
  const origin = inOrigin ?? outOrigin
  if (origin === undefined) return null
  const count = all.get(origin)?.badge?.maxCount ?? own.badge?.maxCount ?? 0
  if (count <= 1) return null
  const side = inOrigin !== undefined && outOrigin !== undefined ? 'both' : inOrigin !== undefined ? 'in' : 'out'
  return { count, side }
}

function NodeWrapper({ node, descriptor, selected, selection, connectedPortIds, multiplicity, stack, overlayTarget, ghostActive, listening }: NodeWrapperProps) {
  const [editing, setEditing] = useState(false)
  const [menuPos, setMenuPos] = useState<{ x: number; y: number } | null>(null)
  const wrapperRef = useRef<HTMLDivElement | null>(null)
  // Removing a focused input from the DOM (as unmounting on Enter/Escape
  // does) can still fire a native 'blur' first in some browsers — this
  // guards against onBlur re-committing (or, worse, committing after
  // Escape already cancelled) once a key handler has already resolved the
  // rename this render pass.
  const renameResolvedRef = useRef(false)

  const displayDescriptor = useMemo<NodeDescriptor>(
    () => (node.titleOverride ? { ...descriptor, title: node.titleOverride } : descriptor),
    [descriptor, node.titleOverride],
  )

  const state: NodeCardState = {
    selected,
    listening,
    bypassed: node.bypassed,
    error: node.error,
    connectedPortIds,
    parameterValues: node.parameterValues,
    onParameterCommit: (id, value) => setParameterValue(node.id, id, value),
    onParameterLive: (id, value) => setParameterLive(node.id, id, value),
    portMultiplicity: multiplicity?.ports,
    instanceCountBadge: multiplicity?.badge,
    macroEnumOptionLabels: node.macroEnumOptionLabels,
    onSetMacroEnumOptionLabels: (labels) => setMacroEnumOptionLabels(node.id, labels),
    countMinOverride: node.countMinOverride,
    countMaxOverride: node.countMaxOverride,
    onSetCountMin: (value) => setCountMin(node.id, value),
    onSetCountMax: (value) => setCountMax(node.id, value),
    viewerRangeMinOverride: node.viewerRangeMinOverride,
    viewerRangeMaxOverride: node.viewerRangeMaxOverride,
    onSetViewerRangeMin: (value) => setViewerRangeMin(node.id, value),
    onSetViewerRangeMax: (value) => setViewerRangeMax(node.id, value),
    viewerCenterOverride: node.viewerCenterOverride,
    onSetViewerCenter: (value) => setViewerCenter(node.id, value),
    previewPlayheadMode: node.previewPlayheadMode,
    onSetPreviewPlayheadMode: (mode) => setPreviewPlayheadMode(node.id, mode),
    overlayTarget,
  }

  const startEditing = () => {
    renameResolvedRef.current = false
    setEditing(true)
  }

  // Ctrl+R (InfiniteCanvas's keyboard handler) asks the selected node to
  // rename itself — the text field is this wrapper's own state.
  useEffect(() => {
    const el = wrapperRef.current
    // Decorations edit their own text (double-click); they have no title.
    if (!el || DECORATION_TYPES.has(node.typeId) || node.typeId === 'deco.reroute') return
    const onRename = () => {
      renameResolvedRef.current = false
      setEditing(true)
    }
    el.addEventListener('bazalt-rename', onRename)
    return () => el.removeEventListener('bazalt-rename', onRename)
  }, [node.typeId])
  const commitRename = (value: string) => {
    if (renameResolvedRef.current) return
    renameResolvedRef.current = true
    renameNode(node.id, value)
    setEditing(false)
  }
  const cancelRename = () => {
    renameResolvedRef.current = true
    setEditing(false)
  }

  // Callback ref (not a layout effect): fires exactly when React attaches
  // this DOM node, which is the earliest point `.node-title`'s real
  // geometry can be measured — see getTitleGeometry's own comment.
  //
  // Width is NOT just the current title's rendered width: a short existing
  // name (e.g. "SVF") measures only a few characters wide, leaving no room
  // to see a longer replacement being typed — direct feedback: "the input
  // field is so small I cannot see what I'm typing". Widened to a sensible
  // minimum, capped so it doesn't overflow past the node card's own right
  // edge (it's absolutely positioned, so temporarily overlapping the
  // title-bar icons while actively renaming is fine).
  const RENAME_INPUT_MIN_WIDTH = 140
  const positionRenameInput = (el: HTMLInputElement | null) => {
    if (!el || !wrapperRef.current) return
    const geometry = getTitleGeometry(wrapperRef.current)
    if (!geometry) return
    const maxWidth = wrapperRef.current.offsetWidth - geometry.left - 8
    const width = Math.max(geometry.width, Math.min(RENAME_INPUT_MIN_WIDTH, maxWidth))
    el.style.left = `${geometry.left}px`
    el.style.top = `${geometry.top}px`
    el.style.width = `${width}px`
    el.style.height = `${geometry.height}px`
  }

  return (
    <div
      className="graph-node-wrapper"
      ref={wrapperRef}
      data-node-instance={node.id}
      style={{ left: node.x, top: node.y }}
      onClickCapture={(e) => {
        if ((e.target as HTMLElement).closest('.node-bypass-icon')) {
          e.stopPropagation()
          toggleBypass(node.id)
        }
      }}
      onDoubleClickCapture={(e) => {
        // A double-click anywhere on a Listen stops listening (wiki/ROADMAP.md stage 0).
        if (node.typeId === 'view.listen') {
          e.stopPropagation()
          deleteNodes([node.id])
          return
        }
        if ((e.target as HTMLElement).closest('.node-title')) {
          e.stopPropagation()
          startEditing()
        }
      }}
      onContextMenu={(e) => {
        // While placing a node, right-click cancels placement instead of
        // opening this node's own menu (blueprint §6.2) — don't claim the
        // event, let it bubble to InfiniteCanvas's own right-click handling.
        if (ghostActive) return
        e.preventDefault()
        e.stopPropagation()
        // Right-clicking a node NOT already in the current multi-selection
        // replaces the selection with just that node (most editors'
        // convention); right-clicking one that IS part of it leaves the
        // selection alone, so the menu's bulk actions apply to the whole
        // group (M10_REVIEW.md §14).
        if (!selection.has(node.id)) setSelection([node.id])
        setMenuPos({ x: e.clientX, y: e.clientY })
      }}
    >
      {stack && (
        <>
          <div className={`graph-node-stack graph-node-stack-${stack.side}`} aria-hidden>
            <div className="graph-node-stack-card graph-node-stack-card-2" />
            <div className="graph-node-stack-card graph-node-stack-card-1" />
          </div>
          {/* The allocator shows its own live "active/max" readout instead. */}
          {!multiplicity?.badge && <span className="graph-node-stack-badge">×{stack.count}</span>}
        </>
      )}
      {DECORATION_TYPES.has(node.typeId) || node.typeId === 'deco.reroute' ? (
        <DecorationCard node={node} descriptor={descriptor} selected={selected} connectedPortIds={connectedPortIds} />
      ) : (
        <NodeCard descriptor={displayDescriptor} state={state} instanceId={node.id} />
      )}
      {editing && (
        <input
          ref={positionRenameInput}
          className="graph-node-rename-input"
          autoFocus
          defaultValue={displayDescriptor.title}
          onFocus={(e) => e.currentTarget.select()}
          onClick={(e) => e.stopPropagation()}
          onMouseDown={(e) => e.stopPropagation()}
          onBlur={(e) => commitRename(e.currentTarget.value)}
          onKeyDown={(e) => {
            if (e.key === 'Enter') commitRename(e.currentTarget.value)
            else if (e.key === 'Escape') cancelRename()
          }}
        />
      )}
      {menuPos &&
        overlayTarget &&
        createPortal(
          <NodeContextMenu
            x={menuPos.x}
            y={menuPos.y}
            bypassed={node.bypassed}
            onRename={() => {
              // Rename always targets the single node right-clicked, even
              // within a multi-selection — there's only one text field, so
              // "act on the whole selection" has no sensible meaning here.
              setMenuPos(null)
              startEditing()
            }}
            onToggleBypass={() => {
              setMenuPos(null)
              if (selection.has(node.id) && selection.size > 1) toggleBypassMany([...selection])
              else toggleBypass(node.id)
            }}
            onSetAsOutput={() => {
              // Always targets the single node right-clicked, same as
              // Rename above — "set several nodes as the output" has no
              // sensible meaning (there is exactly one graph output).
              setMenuPos(null)
              setAsOutput(node.id)
            }}
            onDelete={() => {
              setMenuPos(null)
              if (selection.has(node.id) && selection.size > 1) deleteNodes([...selection])
              else deleteNodes([node.id])
            }}
            onClose={() => setMenuPos(null)}
          />,
          overlayTarget,
        )}
    </div>
  )
}

interface GraphSurfaceProps {
  nodes: readonly GraphNode[]
  wires: readonly GraphWire[]
  selection: ReadonlySet<string>
  getDescriptor: (typeId: string) => NodeDescriptor | undefined
  /** Per-node Scalar/Poly port multiplicity plus the live instance-count
      badge, as of each node's last successful compile — graphStore.ts's own
      `multiplicity` snapshot field, a plain lookup by node id (DomainDot's
      real replacement, wiki/plans/DomainRedesign.md Batch 4). Absent/empty
      is fine (NodeCard.tsx's InstanceCountBadge and per-port colouring just
      fall back to their own mock-only defaults) — this is a debugging aid
      for colour/badge display, never load-bearing on connectivity itself.
  */
  multiplicity: ReadonlyMap<string, NodeMultiplicity>
  ghost: GhostPlacement | null
  /** The ghost wrapper's DOM node, exposed so InfiniteCanvas's mousemove
      handler can set its world position directly (style.left/top) every
      frame without going through React state — ghost mount/unmount
      (whether `ghost` is null) is the only thing that re-renders here,
      matching the same "position is imperative, presence is React state"
      split node-drag uses.
  */
  ghostElementRef?: RefObject<HTMLDivElement | null>
  overlayTarget: HTMLElement | null
}

export function GraphSurface({ nodes, wires, selection, getDescriptor, multiplicity, ghost, ghostElementRef, overlayTarget }: GraphSurfaceProps) {
  const ghostActive = ghost !== null
  const ghostDescriptor = ghost ? getDescriptor(ghost.typeId) : undefined

  // Real connectivity, per node — every input/output port id that has a
  // wire touching it, either end. This is what makes the dot-vs-typed-glyph
  // and fallback-value-pill states (NodeCard.tsx) actually reflect the live
  // graph instead of only ever working in the M9 gallery's demo data.
  const connectionsByNode = useMemo(() => {
    const map = new Map<string, Set<string>>()
    const mark = (nodeId: string, portId: string) => {
      let set = map.get(nodeId)
      if (!set) {
        set = new Set()
        map.set(nodeId, set)
      }
      set.add(portId)
    }
    for (const wire of wires) {
      mark(wire.fromNodeId, wire.fromPortId)
      mark(wire.toNodeId, wire.toPortId)
    }
    return map
  }, [wires])

  // Nodes a Listen is fed from (wiki/ROADMAP.md stage 0) — drawn as listening.
  const listenedNodeIds = useMemo(() => {
    const listenIds = new Set(nodes.filter((n) => n.typeId === 'view.listen').map((n) => n.id))
    return new Set(wires.filter((w) => listenIds.has(w.toNodeId)).map((w) => w.fromNodeId))
  }, [nodes, wires])

  return (
    <div className="graph-surface">
      {/* Boxes and images are backgrounds (wiki/plans/Decorations.md): drawn
          first, so every node and cable-anchor sits on top of them. */}
      {[...nodes.filter((n) => BACKGROUND_DECORATIONS.has(n.typeId)), ...nodes.filter((n) => !BACKGROUND_DECORATIONS.has(n.typeId))].map((node) => {
        // Not the getDescriptor prop (a plain typeId lookup) — a macro
        // configured by addMacroFromPort (graphStore.ts) needs its own
        // per-instance output/parameter shape, which only
        // resolveNodeDescriptor knows how to build; every other node falls
        // through to the exact same lookup getDescriptor would have done.
        const descriptor = resolveNodeDescriptor(node)
        if (!descriptor) return null
        return (
          <NodeWrapper
            key={node.id}
            node={node}
            descriptor={descriptor}
            selected={selection.has(node.id)}
            selection={selection}
            connectedPortIds={connectionsByNode.get(node.id)}
            multiplicity={multiplicity.get(node.id)}
            stack={instanceStackFor(descriptor, multiplicity.get(node.id), multiplicity)}
            overlayTarget={overlayTarget}
            ghostActive={ghostActive}
            listening={listenedNodeIds.has(node.id)}
          />
        )
      })}
      {ghost && ghostDescriptor && (
        <div className="graph-node-ghost" ref={ghostElementRef}>
          <NodeCard descriptor={ghostDescriptor} />
        </div>
      )}
      {ghost?.fragment && <FragmentGhost fragment={ghost.fragment} elementRef={ghostElementRef} />}
    </div>
  )
}

/** A pasted or duplicated selection following the cursor: every copied node
    drawn at its offset from the copy's top-left, which is the point the
    canvas positions on the cursor. */
function FragmentGhost({ fragment, elementRef }: { fragment: NonNullable<GhostPlacement['fragment']>; elementRef?: RefObject<HTMLDivElement | null> }) {
  const previewNodes = useMemo(() => fragmentPreviewNodes(fragment), [fragment])
  return (
    <div className="graph-node-ghost" ref={elementRef}>
      {previewNodes.map((node) => {
        const descriptor = resolveNodeDescriptor(node)
        if (!descriptor) return null
        return (
          <div key={node.id} className="graph-node-ghost-item" style={{ left: node.x, top: node.y }}>
            {DECORATION_TYPES.has(node.typeId) || node.typeId === 'deco.reroute' ? (
              <DecorationCard node={node} descriptor={descriptor} selected={false} />
            ) : (
              <NodeCard descriptor={node.titleOverride ? { ...descriptor, title: node.titleOverride } : descriptor} state={{ parameterValues: node.parameterValues }} />
            )}
          </div>
        )
      })}
    </div>
  )
}
