#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace bazalt::engine
{
    /** ARCHITECTURE.md §6.2: a small header plus a raw float32 payload,
        binary (not JSON) end to end. Every field is fixed-size and
        little-endian on every platform this project targets (x64), so the
        header can be reinterpreted directly by JS via DataView without a
        parsing step on either side.
    */
    enum class TelemetryFrameType : uint32_t
    {
        Oscilloscope = 0,
        Spectrum = 1,
        Meter = 2,
        // view.ripple (design/Visualization/Ripple.png): a list of event
        // ages, not a fixed-shape buffer like the three above — each
        // payload float is "how many seconds ago this event fired",
        // oldest-dropped-first if a drain finds more than
        // AnalysisThread::maxEventsPerPublish of them. Variable length
        // between publishes is already handled by the existing frame
        // machinery (telemetryClient.ts's getInterpolatedTap falls back to
        // "just the latest payload, no interpolation" whenever two
        // consecutive frames' lengths differ, which they almost always
        // will here) — nothing about the header/payload framing itself
        // needed to change for this.
        EventImpulse = 3,
        // design/Visualization/Scope1.png: a long-window scrolling history —
        // fixed-shape (AnalysisThread::maxHistoryColumns (lo, hi) pairs,
        // oldest first) exactly like Oscilloscope's own payload, but
        // computed completely differently: Oscilloscope recomputes its
        // whole window from the tap's raw sample ring every drain (capped
        // at the ring's own 8192-sample depth, ~0.2s at 44.1kHz — nowhere
        // near "tens of seconds"), while this is built incrementally,
        // decimating only the NEW samples seen since the last drain into a
        // persistent per-tap ring of columns (AnalysisThread's own
        // historyColumnLo/Hi/Head/ElapsedSeconds state), so an arbitrarily
        // long window costs a few hundred floats of state per tap, not
        // hundreds of thousands of raw samples. See
        // AnalysisThread::publishRollingHistory for the actual algorithm.
        RollingHistory = 4
    };

    struct TelemetryFrameHeader
    {
        uint32_t tapId = 0;
        TelemetryFrameType frameType = TelemetryFrameType::Oscilloscope;
        float sampleRate = 0.0f;
        uint64_t sequenceNumber = 0;
        uint32_t payloadNumFloats = 0;
    };

    /** The largest spectrum an analysis frame can carry (an 8192-point FFT has
        4096 bins) and so the size every frame buffer and every reader's copy
        buffer must allow for: header plus that many floats. One constant, so
        the analysis thread, the hub and the WebView resource provider cannot
        drift apart (they were three separate 16384s before ADR-0029).
    */
    inline constexpr size_t maxSpectrumBins = 4096;
    inline constexpr size_t maxTelemetryFrameBytes = sizeof (TelemetryFrameHeader) + maxSpectrumBins * sizeof (float);

    /** Packs header + payload into a contiguous byte buffer (header first,
        raw float32 payload immediately after, no padding). Writes into
        `out`, which the caller owns and preallocates — this runs on the
        analysis thread, not the audio thread, so allocating here is fine,
        but callers on a hot path should still reuse `out` across calls
        rather than constructing a fresh vector every time.
    */
    inline void serializeTelemetryFrame (const TelemetryFrameHeader& header,
                                         const float* payload,
                                         uint32_t payloadNumFloats,
                                         std::vector<std::byte>& out)
    {
        TelemetryFrameHeader headerToWrite = header;
        headerToWrite.payloadNumFloats = payloadNumFloats;

        const auto totalBytes = sizeof (TelemetryFrameHeader) + (size_t) payloadNumFloats * sizeof (float);
        out.resize (totalBytes);

        std::memcpy (out.data(), &headerToWrite, sizeof (TelemetryFrameHeader));

        if (payloadNumFloats > 0)
            std::memcpy (out.data() + sizeof (TelemetryFrameHeader), payload, (size_t) payloadNumFloats * sizeof (float));
    }

    /** Returns false (and leaves headerOut/payloadOut untouched) if `data`
        is too short to contain a valid header + declared payload — this is
        the one thing that must be checked before reinterpreting, since
        `data` may come from an untrusted or torn read.
    */
    inline bool parseTelemetryFrame (const std::byte* data, size_t dataSize,
                                     TelemetryFrameHeader& headerOut,
                                     const float*& payloadOut)
    {
        if (dataSize < sizeof (TelemetryFrameHeader))
            return false;

        TelemetryFrameHeader header;
        std::memcpy (&header, data, sizeof (TelemetryFrameHeader));

        const auto expectedSize = sizeof (TelemetryFrameHeader) + (size_t) header.payloadNumFloats * sizeof (float);
        if (dataSize < expectedSize)
            return false;

        headerOut = header;
        payloadOut = reinterpret_cast<const float*> (data + sizeof (TelemetryFrameHeader));
        return true;
    }
}
