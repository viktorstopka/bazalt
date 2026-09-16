// Mirrors engine/include/bazalt/engine/telemetry/TelemetryFrame.h's binary
// layout exactly (header comment there: "every field is fixed-size and
// little-endian on every platform this project targets... reinterpreted
// directly by JS via DataView without a parsing step"). Header is 32 bytes
// on x64 (8-byte alignment padding after sampleRate, before sequenceNumber):
//   offset 0  uint32  tapId
//   offset 4  uint32  frameType
//   offset 8  float32 sampleRate
//   offset 12 -       (compiler padding, ignored)
//   offset 16 uint64  sequenceNumber
//   offset 24 uint32  payloadNumFloats
//   offset 28 -       (padding to 32, ignored)
//   offset 32 float32[payloadNumFloats] payload

export const TELEMETRY_HEADER_SIZE = 32

// A plain object, not `enum` — this repo's TS config runs with
// erasableSyntaxOnly (TS 6), which disallows enum since it generates
// runtime code rather than being purely type-level.
export const TelemetryFrameType = {
  Oscilloscope: 0,
  Spectrum: 1,
  Meter: 2,
} as const
export type TelemetryFrameType = (typeof TelemetryFrameType)[keyof typeof TelemetryFrameType]

export interface TelemetryFrame {
  tapId: number
  frameType: TelemetryFrameType
  sampleRate: number
  sequenceNumber: bigint
  payload: Float32Array
}

/** Returns null if `buffer` is too short for a valid header + declared
    payload — mirrors parseTelemetryFrame()'s C++ contract (TelemetryFrame.h):
    this is the one thing that must be checked before trusting the data,
    since it may come from a torn or empty response.
*/
export function parseTelemetryFrame(buffer: ArrayBuffer): TelemetryFrame | null {
  if (buffer.byteLength < TELEMETRY_HEADER_SIZE) return null

  const view = new DataView(buffer)
  const tapId = view.getUint32(0, true)
  const frameType = view.getUint32(4, true) as TelemetryFrameType
  const sampleRate = view.getFloat32(8, true)
  const sequenceNumber = view.getBigUint64(16, true)
  const payloadNumFloats = view.getUint32(24, true)

  const expectedSize = TELEMETRY_HEADER_SIZE + payloadNumFloats * 4
  if (buffer.byteLength < expectedSize) return null

  const payload = new Float32Array(buffer, TELEMETRY_HEADER_SIZE, payloadNumFloats)
  return { tapId, frameType, sampleRate, sequenceNumber, payload }
}
