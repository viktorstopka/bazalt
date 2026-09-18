// React binding for graphStore.ts's external store (see that file's header
// comment for why it's a plain module rather than a state library).
import { useSyncExternalStore } from 'react'
import { subscribe, getSnapshot, type GraphSnapshot } from './graphStore'

export function useGraphSnapshot(): GraphSnapshot {
  return useSyncExternalStore(subscribe, getSnapshot)
}
