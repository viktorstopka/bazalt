// The user's saved patches (plugin/source/UserPatchLibrary.h): files in the
// per-user app-data folder, shared by the VST3 and the Standalone. Every call
// degrades to "nothing saved" outside the plugin's WebView (the plain Vite
// dev page has no native functions).
import { getNativeFunction } from '@juce-framework/webview'

export interface UserPatchEntry {
  name: string
  fileName: string
  modifiedAtMs: number
}

export interface SaveResult {
  success: boolean
  errorMessage: string
  fileName: string
}

async function call<T>(name: string, fallback: T, ...args: unknown[]): Promise<T> {
  try {
    return ((await getNativeFunction(name)(...args)) as T) ?? fallback
  } catch {
    return fallback
  }
}

export const listUserPatches = () => call<UserPatchEntry[]>('patchList', [])
export const userPatchExists = (name: string) => call<boolean>('patchExists', false, name)
export const saveUserPatch = (name: string) =>
  call<SaveResult>('patchSave', { success: false, errorMessage: 'Saving needs the plugin', fileName: '' }, name)
export const loadUserPatchJson = (fileName: string) => call<string>('patchLoad', '', fileName)
export const deleteUserPatch = (fileName: string) => call<boolean>('patchDelete', false, fileName)
