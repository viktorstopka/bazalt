// wiki/plans/Sampling.md 3a.1: the UI's side of the sample library
// (plugin/source/SampleLibrary.h). Samples are decoded and stored by the
// plugin; the UI sends a dropped file's bytes and asks for facts and a
// waveform overview to draw. Every call degrades to "nothing" outside the
// plugin's WebView.
import { getNativeFunction } from '@juce-framework/webview'

export interface SampleInfo {
  found: boolean
  id?: string
  name?: string
  channels?: number
  frames?: number
  sampleRate?: number
  seconds?: number
}

export interface SampleImportResult extends SampleInfo {
  success: boolean
  errorMessage: string
}

async function call<T>(name: string, fallback: T, ...args: unknown[]): Promise<T> {
  if (typeof window.__JUCE__ === 'undefined') return fallback
  try {
    return ((await getNativeFunction(name)(...args)) as T) ?? fallback
  } catch {
    return fallback
  }
}

const AUDIO_EXTENSIONS = /\.(wav|wave|aif|aiff|flac|ogg|mp3)$/i

export function isAudioFile(file: File): boolean {
  return file.type.startsWith('audio/') || AUDIO_EXTENSIONS.test(file.name)
}

function readAsBase64(blob: Blob): Promise<string> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader()
    reader.onload = () => resolve(String(reader.result).replace(/^data:[^,]*,/, ''))
    reader.onerror = () => reject(reader.error)
    reader.readAsDataURL(blob)
  })
}

/** Imports a file into the library; the result carries its id. */
export async function importSampleFile(file: File): Promise<SampleImportResult> {
  const base64 = await readAsBase64(file)
  const result = await call<SampleImportResult>('sampleImport', { success: false, errorMessage: 'Samples need the plugin', found: false }, file.name, base64)
  if (result.success && result.id) {
    infoCache.set(result.id, Promise.resolve(result))
    overviewCache.delete(result.id)
  }
  return result
}

const infoCache = new Map<string, Promise<SampleInfo>>()
const overviewCache = new Map<string, Promise<Float32Array>>()

/** A sample's facts (cached); found: false when the library doesn't have it. */
export function sampleInfo(id: string): Promise<SampleInfo> {
  let cached = infoCache.get(id)
  if (!cached) {
    cached = call<SampleInfo>('sampleInfo', { found: false }, id)
    infoCache.set(id, cached)
    // A missing sample may turn up later (relinked): don't cache the miss.
    void cached.then((info) => {
      if (!info.found) infoCache.delete(id)
    })
  }
  return cached
}

/** `buckets` (min, max) pairs over the whole sample, all channels (cached). */
export function sampleOverview(id: string, buckets: number): Promise<Float32Array> {
  const key = `${id}:${buckets}`
  let cached = overviewCache.get(key)
  if (!cached) {
    cached = call<number[]>('sampleOverview', [], id, buckets).then((values) => Float32Array.from(values))
    overviewCache.set(key, cached)
  }
  return cached
}

/** Republishes every Sample node using `id` (after a relink). */
export function refreshSample(id: string): Promise<boolean> {
  return call<boolean>('sampleRefresh', false, id)
}

export interface ExportResult {
  success: boolean
  errorMessage: string
  path?: string
}

/** "Export with samples…": a save dialog, then one file with every sample embedded. */
export function exportPatchWithSamples(name: string): Promise<ExportResult> {
  return call<ExportResult>('patchExportWithSamples', { success: false, errorMessage: 'Exporting needs the plugin' }, name)
}

/** Opens a file picker for one audio file. */
export function pickAudioFile(): Promise<File | null> {
  return new Promise((resolve) => {
    const input = document.createElement('input')
    input.type = 'file'
    input.accept = 'audio/*,.wav,.aif,.aiff,.flac,.ogg,.mp3'
    input.style.display = 'none'
    document.body.appendChild(input)
    input.onchange = () => {
      resolve(input.files?.[0] ?? null)
      input.remove()
    }
    input.click()
  })
}

export interface SampleAnalysis {
  root?: number | null
  slices?: number[]
}

/** Detected root note and transients (seconds in the sample) — the Sample factory. */
export function analyseSample(id: string, sensitivityDb: number): Promise<SampleAnalysis> {
  return call<SampleAnalysis>('sampleAnalyse', {}, id, sensitivityDb)
}
