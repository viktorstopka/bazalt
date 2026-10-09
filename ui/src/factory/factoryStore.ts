// Which node's Factory window is open (wiki/plans/DataAndWavetable.md D10) —
// one at a time, replacing the canvas while it is.
import { useSyncExternalStore } from 'react'

let openNodeId: string | null = null
const listeners = new Set<() => void>()

export function openFactory(nodeId: string): void {
  openNodeId = nodeId
  listeners.forEach((l) => l())
}

export function closeFactory(): void {
  openNodeId = null
  listeners.forEach((l) => l())
}

export function isFactoryOpen(): boolean {
  return openNodeId !== null
}

export function useOpenFactory(): string | null {
  return useSyncExternalStore(
    (listener) => {
      listeners.add(listener)
      return () => listeners.delete(listener)
    },
    () => openNodeId,
  )
}
