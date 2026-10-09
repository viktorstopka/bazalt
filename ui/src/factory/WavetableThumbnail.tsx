// data.wavetable's compact preview on its node (wiki/plans/DataAndWavetable.md
// D10): a few frames of the table stacked back to front, with the same Edit
// button a curve factory has.
import { useMemo } from 'react'
import { type WavetableDoc, frameCycle, type Keyframe } from './wavetableModel'
import { openFactory } from './factoryStore'
import { tokens } from '../theme/tokens'
import './CurveThumbnail.css'

const WIDTH = 160
const HEIGHT = 44
const FRAMES = 6

export function WavetableThumbnail({ doc, nodeId }: { doc: WavetableDoc; nodeId?: string }) {
  const paths = useMemo(() => {
    const cache = new Map<Keyframe, number[]>()
    const waveW = WIDTH - 30
    const waveH = HEIGHT - 16
    return Array.from({ length: FRAMES }, (_, i) => {
      const f = i / (FRAMES - 1)
      const x0 = 2 + f * 26
      const yMid = HEIGHT - 3 - waveH / 2 - f * 10
      const cycle = frameCycle(doc, f, 64, cache)
      return cycle.map((v, j) => `${j === 0 ? 'M' : 'L'}${(x0 + (j / 63) * waveW).toFixed(1)},${(yMid - v * waveH * 0.5).toFixed(1)}`).join('')
    }).reverse()
  }, [doc])
  const open = () => nodeId && openFactory(nodeId)
  return (
    <div className="curve-thumbnail">
      <svg
        className="curve-thumbnail-svg"
        viewBox={`0 0 ${WIDTH} ${HEIGHT}`}
        preserveAspectRatio="none"
        onDoubleClick={(e) => {
          e.stopPropagation()
          open()
        }}
      >
        {paths.map((d, i) => (
          <path key={i} d={d} stroke={tokens.color.portModulation} className="curve-thumbnail-path" opacity={0.25 + (0.75 * (i + 1)) / FRAMES} />
        ))}
      </svg>
      {nodeId && (
        <button
          className="curve-thumbnail-edit"
          onMouseDown={(e) => e.stopPropagation()}
          onClick={(e) => {
            e.stopPropagation()
            open()
          }}
          title="Edit the wavetable"
        >
          Edit
        </button>
      )}
    </div>
  )
}
