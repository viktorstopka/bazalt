#pragma once

#include "bazalt/engine/graph/PreviewDescriptor.h"
#include <algorithm>

namespace bazalt::engine
{
    /** ADR-0029: what one subscribed tap should be analysed as. Everything a
        viewer lets the user change lives here, so `AnalysisThread` - which
        already owns every FFT and every meter ballistic - is the only place
        that applies it and nothing is ever computed in the UI (CLAUDE.md
        rule 1).

        The defaults are exactly what every tap got before ADR-0029: the whole
        window the tap holds, a 2048-point FFT with no tilt or smoothing, and
        the peak meter. So a tap nobody configures - the five baseline taps of
        the M5 analysis panel - behaves as it always did.
    */
    struct TapSettings
    {
        static constexpr int minFftOrder = 9;   // 512 points
        static constexpr int maxFftOrder = 13;  // 8192 points, the tap ring's own capacity
        static constexpr int defaultFftOrder = 11;

        // ---- Oscilloscope ----
        /** How much of the tap to show, in seconds. 0 means "everything the tap
            holds" (the pre-ADR-0029 behaviour). Longer than the tap's ring
            simply shows the whole ring: the ring (8192 samples) is the cap.
        */
        float scopeWindowSeconds = 0.0f;
        ScopeTriggerMode scopeTrigger = ScopeTriggerMode::Free;

        // ---- Spectrum ----
        int fftOrder = defaultFftOrder;
        float spectrumTiltDbPerOctave = 0.0f; // about 1 kHz
        float spectrumAveraging = 0.0f;       // 0 = none, towards 1 = heavier smoothing

        // ---- Meter ----
        MeterMode meterMode = MeterMode::Peak;

        /** What a node's declared preview asks for. `Histogram` and `PerNote`
            (ADR-0029, Q2) have no analysis behind them yet, so they fall back
            to the nearest thing that does, rather than being silently
            mis-drawn.
        */
        static TapSettings fromPreview (const PreviewDescriptor& preview) noexcept
        {
            TapSettings settings;
            settings.scopeWindowSeconds = std::max (0.0f, preview.timeWindowSeconds);
            settings.scopeTrigger = preview.triggerMode == ScopeTriggerMode::RisingEdge ? ScopeTriggerMode::RisingEdge
                                                                                        : ScopeTriggerMode::Free;

            auto order = minFftOrder;
            while (order < maxFftOrder && (1 << order) < preview.fftSize)
                ++order;
            settings.fftOrder = order;

            settings.spectrumTiltDbPerOctave = preview.tiltDbPerOctave;
            settings.spectrumAveraging = std::clamp (preview.averaging, 0.0f, 0.99f);
            settings.meterMode = preview.meterMode == MeterMode::Histogram ? MeterMode::Peak : preview.meterMode;
            return settings;
        }
    };
}
