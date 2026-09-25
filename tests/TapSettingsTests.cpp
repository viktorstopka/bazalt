// ADR-0029, step 3: every field of TapSettings really changes what
// AnalysisThread publishes. Each test feeds a tap a known signal, runs exactly
// one analysis pass synchronously (processSlotForTesting - the thread is never
// started, so nothing races) and reads the published frame back.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/telemetry/AnalysisThread.h"
#include "bazalt/engine/telemetry/TelemetryHub.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace bazalt::engine;
using Catch::Approx;

namespace
{
    constexpr double sampleRate = 44100.0;
    constexpr int ringSize = 8192;
    constexpr double pi = 3.14159265358979323846;

    struct Rig
    {
        TelemetryHub hub;
        AnalysisThread analysis { hub };
        Tap* tap = nullptr;
        static constexpr size_t slot = 0;

        Rig()
        {
            hub.prepare ((size_t) ringSize, maxTelemetryFrameBytes);
            analysis.prepare (sampleRate);
            tap = hub.subscribeTap ("t");
        }

        void configure (const TapSettings& settings) { hub.setTapSettings ("t", settings); }

        // Fills the whole ring, so every drain sees exactly these 8192 samples.
        template <typename Generator>
        void fill (Generator generator)
        {
            std::vector<float> samples ((size_t) ringSize);
            for (int i = 0; i < ringSize; ++i)
                samples[(size_t) i] = generator (i);

            tap->push (samples.data(), ringSize);
        }

        void analyse (double elapsedSeconds = 0.01) { analysis.processSlotForTesting (slot, elapsedSeconds); }

        std::vector<float> frame (TelemetryFrameType type) const
        {
            std::vector<std::byte> bytes (maxTelemetryFrameBytes);
            const auto size = hub.getFrameBufferBySlot (slot, type)->readLatest (bytes.data(), bytes.size());

            TelemetryFrameHeader header;
            const float* payload = nullptr;
            REQUIRE (parseTelemetryFrame (bytes.data(), size, header, payload));
            return std::vector<float> (payload, payload + header.payloadNumFloats);
        }
    };

    float loudestBin (const std::vector<float>& spectrum) { return *std::max_element (spectrum.begin(), spectrum.end()); }

    size_t loudestBinIndex (const std::vector<float>& spectrum)
    {
        return (size_t) std::distance (spectrum.begin(), std::max_element (spectrum.begin(), spectrum.end()));
    }
}

TEST_CASE ("TapSettings::fromPreview maps a declaration, and falls back where there is no analysis yet",
           "[engine][telemetry][ADR-0029]")
{
    PreviewDescriptor preview;
    preview.timeWindowSeconds = 0.02f;
    preview.triggerMode = ScopeTriggerMode::RisingEdge;
    preview.fftSize = 4096;
    preview.tiltDbPerOctave = 3.0f;
    preview.averaging = 0.5f;
    preview.meterMode = MeterMode::TruePeak;

    auto settings = TapSettings::fromPreview (preview);
    CHECK (settings.scopeWindowSeconds == Approx (0.02f));
    CHECK (settings.scopeTrigger == ScopeTriggerMode::RisingEdge);
    CHECK (settings.fftOrder == 12);
    CHECK (settings.spectrumTiltDbPerOctave == Approx (3.0f));
    CHECK (settings.spectrumAveraging == Approx (0.5f));
    CHECK (settings.meterMode == MeterMode::TruePeak);

    // Not built yet (ADR-0029 Q2): PerNote and Histogram fall back, never mis-draw.
    preview.triggerMode = ScopeTriggerMode::PerNote;
    preview.meterMode = MeterMode::Histogram;
    settings = TapSettings::fromPreview (preview);
    CHECK (settings.scopeTrigger == ScopeTriggerMode::Free);
    CHECK (settings.meterMode == MeterMode::Peak);

    // An FFT size between two powers of two rounds up; out of range is clamped.
    preview.fftSize = 3000;
    CHECK (TapSettings::fromPreview (preview).fftOrder == 12);
    preview.fftSize = 16;
    CHECK (TapSettings::fromPreview (preview).fftOrder == TapSettings::minFftOrder);
    preview.fftSize = 1 << 20;
    CHECK (TapSettings::fromPreview (preview).fftOrder == TapSettings::maxFftOrder);
}

TEST_CASE ("A tap's settings round-trip through the hub, and a fresh claim starts from the defaults",
           "[engine][telemetry][ADR-0029]")
{
    Rig rig;
    TapSettings settings;
    settings.scopeWindowSeconds = 0.01f;
    settings.fftOrder = 13;
    settings.meterMode = MeterMode::Rms;
    rig.configure (settings);

    auto read = rig.hub.getTapSettingsBySlot (Rig::slot);
    CHECK (read.scopeWindowSeconds == Approx (0.01f));
    CHECK (read.fftOrder == 13);
    CHECK (read.meterMode == MeterMode::Rms);

    // An idempotent re-subscribe keeps the settings (the processor re-attaches on every edit).
    rig.hub.subscribeTap ("t");
    CHECK (rig.hub.getTapSettingsBySlot (Rig::slot).fftOrder == 13);

    // A slot given to a different name starts over.
    rig.hub.unsubscribeTap ("t");
    rig.hub.subscribeTap ("other");
    read = rig.hub.getTapSettingsBySlot (Rig::slot);
    CHECK (read.scopeWindowSeconds == 0.0f);
    CHECK (read.fftOrder == TapSettings::defaultFftOrder);
    CHECK (read.meterMode == MeterMode::Peak);

    // Out-of-range orders are clamped rather than trusted.
    settings.fftOrder = 99;
    rig.hub.setTapSettings ("other", settings);
    CHECK (rig.hub.getTapSettingsBySlot (Rig::slot).fftOrder == TapSettings::maxFftOrder);
}

TEST_CASE ("scope: timeWindow shows only the newest part of the tap", "[engine][telemetry][ADR-0029]")
{
    Rig rig;
    rig.fill ([] (int i) { return (float) i / (float) (ringSize - 1); }); // a ramp 0 -> 1

    // Default (0): everything the tap holds, so the trace spans the whole ramp.
    rig.analyse();
    auto scope = rig.frame (TelemetryFrameType::Oscilloscope);
    REQUIRE (scope.size() == 256);
    CHECK (scope[0] < 0.02f);
    CHECK (scope[scope.size() - 1] > 0.98f);

    // 10 ms is ~441 samples: only the top 5% of the ramp.
    TapSettings settings;
    settings.scopeWindowSeconds = 0.01f;
    rig.configure (settings);
    rig.analyse();
    scope = rig.frame (TelemetryFrameType::Oscilloscope);
    for (size_t b = 0; b < scope.size(); ++b)
        CHECK (scope[b] >= 0.94f);

    // Longer than the ring simply shows the ring - the documented cap.
    settings.scopeWindowSeconds = 5.0f;
    rig.configure (settings);
    rig.analyse();
    scope = rig.frame (TelemetryFrameType::Oscilloscope);
    CHECK (scope[0] < 0.02f);
}

TEST_CASE ("scope: a rising-edge trigger starts the trace on a rising crossing", "[engine][telemetry][ADR-0029]")
{
    Rig rig;
    rig.fill ([] (int i) { return (float) std::sin (2.0 * pi * i / 100.0); }); // period 100 samples

    TapSettings settings;
    settings.scopeWindowSeconds = (float) (200.0 / sampleRate); // exactly two periods
    rig.configure (settings);

    // Free: the newest 200 samples start at index 7992, 92% through a cycle.
    rig.analyse();
    auto scope = rig.frame (TelemetryFrameType::Oscilloscope);
    CHECK (scope[0] < -0.3f); // the first bucket's lo (index 0): well below the crossing
    CHECK (scope[1] < 0.0f);  // ... and its hi is still negative

    // Rising edge: it starts at the latest crossing that leaves a full window (index 7900).
    settings.scopeTrigger = ScopeTriggerMode::RisingEdge;
    rig.configure (settings);
    rig.analyse();
    scope = rig.frame (TelemetryFrameType::Oscilloscope);
    CHECK (scope[0] > -0.01f);        // bucket 0 lo
    CHECK (scope[1] >= 0.0f);         // bucket 0 hi
    CHECK (scope[1] < 0.12f);
    CHECK (scope[2 * 20 + 1] > 0.5f); // twenty buckets on the wave has climbed
}

TEST_CASE ("scope: a trigger on a flat signal, or one that never rises, stays free without error",
           "[engine][telemetry][ADR-0029]")
{
    Rig rig;
    TapSettings settings;
    settings.scopeWindowSeconds = 0.01f;
    settings.scopeTrigger = ScopeTriggerMode::RisingEdge;
    rig.configure (settings);

    rig.fill ([] (int) { return 0.5f; });
    rig.analyse();
    auto scope = rig.frame (TelemetryFrameType::Oscilloscope);
    for (auto value : scope)
        CHECK (value == Approx (0.5f));

    rig.fill ([] (int i) { return 1.0f - (float) i / (float) ringSize; }); // only ever falls
    rig.analyse();
    scope = rig.frame (TelemetryFrameType::Oscilloscope);
    CHECK (scope.size() == 256);
}

TEST_CASE ("spectrum: fftSize sets the number of bins and puts a tone in the right one", "[engine][telemetry][ADR-0029]")
{
    Rig rig;
    rig.fill ([] (int i) { return (float) std::sin (2.0 * pi * 1000.0 * i / sampleRate); });

    for (int order = TapSettings::minFftOrder; order <= TapSettings::maxFftOrder; ++order)
    {
        TapSettings settings;
        settings.fftOrder = order;
        rig.configure (settings);
        rig.analyse();

        const auto spectrum = rig.frame (TelemetryFrameType::Spectrum);
        INFO ("fft order " << order);
        REQUIRE (spectrum.size() == (size_t) (1 << order) / 2);

        const auto expectedBin = 1000.0 / (sampleRate / (double) (1 << order));
        CHECK ((double) loudestBinIndex (spectrum) == Approx (expectedBin).margin (1.0));
    }
}

TEST_CASE ("spectrum: tilt applies dB per octave about 1 kHz", "[engine][telemetry][ADR-0029]")
{
    Rig rig;
    rig.fill ([] (int i) { return (float) std::sin (2.0 * pi * 4000.0 * i / sampleRate); }); // two octaves above 1 kHz

    rig.analyse();
    const auto flat = loudestBin (rig.frame (TelemetryFrameType::Spectrum));

    TapSettings settings;
    settings.spectrumTiltDbPerOctave = 3.0f;
    rig.configure (settings);
    rig.analyse();
    const auto tiltedUp = loudestBin (rig.frame (TelemetryFrameType::Spectrum));

    settings.spectrumTiltDbPerOctave = -3.0f;
    rig.configure (settings);
    rig.analyse();
    const auto tiltedDown = loudestBin (rig.frame (TelemetryFrameType::Spectrum));

    // +3 dB/oct over 2 octaves = +6 dB (x1.995); -3 dB/oct = -6 dB (x0.501).
    CHECK (tiltedUp / flat == Approx (1.995f).epsilon (0.03));
    CHECK (tiltedDown / flat == Approx (0.501f).epsilon (0.03));
}

TEST_CASE ("spectrum: averaging smooths over time, and a new FFT size starts a fresh average",
           "[engine][telemetry][ADR-0029]")
{
    Rig rig;
    TapSettings settings;
    settings.spectrumAveraging = 0.9f;
    rig.configure (settings);

    rig.fill ([] (int i) { return (float) std::sin (2.0 * pi * 1000.0 * i / sampleRate); });
    rig.analyse();
    const auto loud = loudestBin (rig.frame (TelemetryFrameType::Spectrum));
    REQUIRE (loud > 1.0f);

    // Then silence: with smoothing the display decays instead of dropping to nothing.
    rig.fill ([] (int) { return 0.0f; });
    rig.analyse();
    CHECK (loudestBin (rig.frame (TelemetryFrameType::Spectrum)) == Approx (0.9f * loud).epsilon (0.01));

    rig.analyse();
    CHECK (loudestBin (rig.frame (TelemetryFrameType::Spectrum)) == Approx (0.81f * loud).epsilon (0.01));

    // A different FFT size is a different set of bins: nothing is carried across.
    settings.fftOrder = 12;
    rig.configure (settings);
    rig.analyse();
    CHECK (loudestBin (rig.frame (TelemetryFrameType::Spectrum)) == 0.0f);

    // With no averaging, silence is silence straight away.
    settings.fftOrder = TapSettings::defaultFftOrder;
    settings.spectrumAveraging = 0.0f;
    rig.configure (settings);
    rig.fill ([] (int i) { return (float) std::sin (2.0 * pi * 1000.0 * i / sampleRate); });
    rig.analyse();
    rig.fill ([] (int) { return 0.0f; });
    rig.analyse();
    CHECK (loudestBin (rig.frame (TelemetryFrameType::Spectrum)) == 0.0f);
}

TEST_CASE ("meter: Peak, Rms and TruePeak read differently on a signal whose peak falls between samples",
           "[engine][telemetry][ADR-0029]")
{
    // A full-scale sine at a quarter of the sample rate, phased so that every
    // sample lands at +-0.7071 while the wave itself reaches 1.0 between them:
    // the classic case a sample-peak meter under-reads by 3 dB.
    Rig rig;
    rig.fill ([] (int i) { return (float) std::sin (0.5 * pi * i + 0.25 * pi); });

    // Peak: the sample peak over an RMS bar - the long-standing behaviour.
    rig.analyse();
    auto meter = rig.frame (TelemetryFrameType::Meter);
    REQUIRE (meter.size() == 2);
    CHECK (meter[0] == Approx (0.7071f).epsilon (0.01));
    CHECK (meter[1] == Approx (0.7071f).epsilon (0.01));

    // TruePeak: the line reaches for the real peak; the bar is unchanged.
    TapSettings settings;
    settings.meterMode = MeterMode::TruePeak;
    rig.configure (settings);
    rig.analyse (10.0); // a long step, so the peak ballistic has fully attacked
    meter = rig.frame (TelemetryFrameType::Meter);
    CHECK (meter[0] > 0.97f);
    CHECK (meter[0] < 1.03f);
    CHECK (meter[1] == Approx (0.7071f).epsilon (0.01));

    // Rms: a single reading - the line rides the bar.
    settings.meterMode = MeterMode::Rms;
    rig.configure (settings);
    rig.analyse();
    meter = rig.frame (TelemetryFrameType::Meter);
    CHECK (meter[0] == meter[1]);
    CHECK (meter[0] == Approx (0.7071f).epsilon (0.01));
}
