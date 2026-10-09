// A factory node's compact live preview (wiki/plans/DataAndWavetable.md D10):
// its curve, drawn small, with an Edit button that opens the Factory window.
// The Shape port's own value when nothing is plugged into it.
import { type CurveDoc, curvePath, domainEnd, valueRange } from './curveModel'
import { openFactory } from './factoryStore'
import { tokens } from '../theme/tokens'
import './CurveThumbnail.css'

const WIDTH = 160
const HEIGHT = 44

export function CurveThumbnail({ doc, nodeId, overridden }: { doc: CurveDoc; nodeId?: string; overridden?: boolean }) {
  const end = domainEnd(doc)
  const [lo, hi] = valueRange(doc)
  const toX = (x: number) => (x / end) * WIDTH
  const toY = (y: number) => HEIGHT - 3 - ((y - lo) / (hi - lo)) * (HEIGHT - 6)
  const open = () => nodeId && openFactory(nodeId)
  return (
    <div className={overridden ? 'curve-thumbnail curve-thumbnail-overridden' : 'curve-thumbnail'}>
      <svg
        className="curve-thumbnail-svg"
        viewBox={`0 0 ${WIDTH} ${HEIGHT}`}
        preserveAspectRatio="none"
        onDoubleClick={(e) => {
          e.stopPropagation()
          open()
        }}
      >
        <line x1={0} x2={WIDTH} y1={toY(lo < 0 ? 0 : lo)} y2={toY(lo < 0 ? 0 : lo)} className="curve-thumbnail-axis" />
        <path d={curvePath(doc, toX, toY, 96)} stroke={tokens.color.portModulation} className="curve-thumbnail-path" />
      </svg>
      {nodeId && (
        <button
          className="curve-thumbnail-edit"
          onMouseDown={(e) => e.stopPropagation()}
          onClick={(e) => {
            e.stopPropagation()
            open()
          }}
          title={overridden ? 'Edit this node’s own curve (the Shape cable plays instead)' : 'Edit the curve'}
        >
          Edit
        </button>
      )}
    </div>
  )
}
