// Image decals (wiki/plans/Decorations.md §4): an image dropped or pasted onto
// the canvas is compressed ONCE, here, before it reaches the patch — patches
// travel inside DAW sessions and get shared, so an untouched 8 MB photo per
// decal would be a real cost. Images are chrome, not sound (CLAUDE.md rule 1
// is about audio), so this belongs in the UI.
//
// - Raster images: downscaled so the longest side is at most MAX_STORED_SIDE
//   (about twice the largest sensible on-screen size, so it stays sharp when
//   zoomed in), re-encoded as WebP; where the WebView can't encode WebP, JPEG
//   for opaque images and PNG for ones with transparency.
// - Kept as they are: SVG (vector, small, rendered through <img>, which runs
//   no scripts) and GIF up to MAX_KEPT_GIF_BYTES (re-encoding would lose an
//   animation).
// The engine stores it once per patch by content and refuses an image that
// would take the patch past its limit (GraphEditController::maxPatchAssetBytes).

export const MAX_STORED_SIDE = 2048
/** The size a new decal appears at (world units), longest side. */
export const INITIAL_DISPLAY_SIDE = 360
const MAX_KEPT_GIF_BYTES = 1024 * 1024
const WEBP_QUALITY = 0.85

export interface ImportedImage {
  mimeType: string
  base64: string
  width: number // initial display size, world units
  height: number
}

function readAsBase64(blob: Blob): Promise<string> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader()
    reader.onload = () => resolve(String(reader.result).replace(/^data:[^,]*,/, ''))
    reader.onerror = () => reject(reader.error)
    reader.readAsDataURL(blob)
  })
}

function naturalSize(blob: Blob): Promise<{ width: number; height: number }> {
  return new Promise((resolve) => {
    const url = URL.createObjectURL(blob)
    const image = new Image()
    image.onload = () => {
      URL.revokeObjectURL(url)
      resolve({ width: image.naturalWidth || 300, height: image.naturalHeight || 200 })
    }
    image.onerror = () => {
      URL.revokeObjectURL(url)
      resolve({ width: 300, height: 200 })
    }
    image.src = url
  })
}

function displaySize(width: number, height: number): { width: number; height: number } {
  const scale = Math.min(1, INITIAL_DISPLAY_SIDE / Math.max(width, height))
  return { width: Math.round(width * scale), height: Math.round(height * scale) }
}

function toBlob(canvas: HTMLCanvasElement, type: string, quality?: number): Promise<Blob | null> {
  return new Promise((resolve) => canvas.toBlob(resolve, type, quality))
}

function hasTransparency(context: CanvasRenderingContext2D, width: number, height: number): boolean {
  const data = context.getImageData(0, 0, width, height).data
  // Every 4th pixel is plenty to spot an alpha channel in use.
  for (let i = 3; i < data.length; i += 16) if (data[i] < 255) return true
  return false
}

export async function importImage(file: Blob): Promise<ImportedImage> {
  if (!file.type.startsWith('image/')) throw new Error('Not an image')

  if (file.type === 'image/svg+xml' || (file.type === 'image/gif' && file.size <= MAX_KEPT_GIF_BYTES)) {
    const size = await naturalSize(file)
    return { mimeType: file.type, base64: await readAsBase64(file), ...displaySize(size.width, size.height) }
  }

  const bitmap = await createImageBitmap(file)
  const scale = Math.min(1, MAX_STORED_SIDE / Math.max(bitmap.width, bitmap.height))
  const width = Math.max(1, Math.round(bitmap.width * scale))
  const height = Math.max(1, Math.round(bitmap.height * scale))
  const canvas = document.createElement('canvas')
  canvas.width = width
  canvas.height = height
  const context = canvas.getContext('2d')
  if (!context) throw new Error('Could not decode the image')
  context.drawImage(bitmap, 0, 0, width, height)
  bitmap.close()

  let blob = await toBlob(canvas, 'image/webp', WEBP_QUALITY)
  if (!blob || blob.type !== 'image/webp') {
    // No WebP encoder in this WebView.
    blob = hasTransparency(context, width, height) ? await toBlob(canvas, 'image/png') : await toBlob(canvas, 'image/jpeg', WEBP_QUALITY)
  }
  if (!blob) throw new Error('Could not encode the image')
  return { mimeType: blob.type, base64: await readAsBase64(blob), ...displaySize(width, height) }
}
