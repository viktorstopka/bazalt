// An EQ factory's compact preview on its node (wiki/plans/DataAndWavetable.md
// D10, 1d): the response on the shared log-frequency axis, ±12 dB, with an
// Edit button that opens the Factory window. Faded when a Curve cable plays
// instead of the node's own bands.
import { useMemo } from 'react'
import { type EqDoc, FREQUENCY_GRID, hzToUnit, responseDb, unitToHz } from './eqModel'
import { openFactory } from './factoryStore'
import { tokens } from '../theme/tokens'
import './CurveThumbnail.css'

const WIDTH = 160
const HEIGHT = 44
const RANGE_DB = 12

export function EqThumbnail({ doc, nodeId, overridden }: { doc: EqDoc; nodeId?: string; overridden?: boolean }) {
  const toY = (db: number) => Math.max(1, Math.min(HEIGHT - 1, HEIGHT / 2 - (db / RANGE_DB) * (HEIGHT / 2 - 3)))
  const path = useMemo(() => {
    let d = ''
    for (let i = 0; i <= 80; i++) {
      const u = i / 80
      d += `${i === 0 ? 'M' : 'L'}${(u * WIDTH).toFixed(1)},${toY(responseDb(doc, unitToHz(u), 48000)).toFixed(1)}`
    }
    return d
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [doc])
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
        {FREQUENCY_GRID.filter((l) => l.label && l.hz > 20 && l.hz < 20000).map((l) => (
          <line key={l.hz} x1={hzToUnit(l.hz) * WIDTH} x2={hzToUnit(l.hz) * WIDTH} y1={0} y2={HEIGHT} className="curve-thumbnail-axis" />
        ))}
        <line x1={0} x2={WIDTH} y1={HEIGHT / 2} y2={HEIGHT / 2} className="curve-thumbnail-axis" />
        <path d={path} stroke={tokens.color.portData} className="curve-thumbnail-path" />
      </svg>
      {nodeId && (
        <button
          className="curve-thumbnail-edit"
          onMouseDown={(e) => e.stopPropagation()}
          onClick={(e) => {
            e.stopPropagation()
            open()
          }}
          title={overridden ? 'Edit this node’s own bands (the Curve cable plays instead)' : 'Edit the EQ'}
        >
          Edit
        </button>
      )}
    </div>
  )
}
