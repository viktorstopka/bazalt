// wiki/plans/Sampling.md 3a.6: a sample made into a wavetable — cycles measured
// along a changing sound, harmonics, texture, and single-/multi-cycle files.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/graph/WavetableExtraction.h"

using namespace bazalt::engine;

namespace
{
    constexpr double rate = 48000.0;

    /** 1 s at 220 Hz turning from a sine into a band-limited saw (harmonic h at 1/h). */
    SampleSource sineIntoSaw()
    {
        SampleSource s;
        s.sampleRate = rate;
        s.frames = (int) rate;
        s.data.resize ((size_t) s.frames);
        for (int i = 0; i < s.frames; ++i)
        {
            const auto t = (double) i / s.frames;
            const auto phase = juce::MathConstants<double>::twoPi * 220.0 * i / rate;
            double saw = 0.0;
            for (int h = 1; h <= 20; ++h)
                saw += std::sin (h * phase) / h;
            s.data[(size_t) i] = (float) (0.5 * ((1.0 - t) * std::sin (phase) + t * saw));
        }
        return s;
    }

    float ratio (const WavetableKeyframe& frame, int harmonic) { return frame.amplitudes[(size_t) harmonic - 1] / frame.amplitudes[0]; }
}

TEST_CASE ("Cycles: frames along a changing sound replay its change, turned to one phase", "[engine][wavetable][3a]")
{
    const auto source = sineIntoSaw();
    const auto result = wavetableExtraction::fromSample (source, { .method = wavetableExtraction::Method::Cycles, .frames = 8 });
    INFO (result.errorMessage);
    REQUIRE (result.success);
    CHECK (result.pitch == Catch::Approx (57.0f).margin (0.1f)); // A3
    const auto& frames = result.document.keyframes;
    REQUIRE (frames.size() == 8);
    CHECK (frames.front().position == 0.0f);
    CHECK (frames.back().position == 1.0f);

    // The first frame is nearly a sine; the last nearly the saw; the middle between.
    CHECK (ratio (frames.front(), 2) < 0.1f);
    CHECK (ratio (frames.back(), 2) == Catch::Approx (0.5f).margin (0.06));
    CHECK (ratio (frames.back(), 3) == Catch::Approx (1.0f / 3.0f).margin (0.05));
    CHECK (ratio (frames[4], 2) > ratio (frames[2], 2));

    // Every frame's fundamental starts at the same phase, so they morph without cancelling.
    for (const auto& frame : frames)
        CHECK (frame.phases[0] == Catch::Approx (frames.front().phases[0]).margin (0.01));

    // Normalised: each frame's cycle peaks at 1, and its shape is the sine's.
    const auto cycle = frames.front().renderCycle();
    float peak = 0.0f;
    for (const auto v : cycle)
        peak = std::max (peak, std::abs (v));
    CHECK (peak == Catch::Approx (1.0f).margin (0.02));
}

TEST_CASE ("Harmonics drops the phases; Texture keeps the colour of noise", "[engine][wavetable][3a]")
{
    const auto source = sineIntoSaw();
    const auto harmonics = wavetableExtraction::fromSample (source, { .method = wavetableExtraction::Method::Harmonics, .frames = 4 });
    REQUIRE (harmonics.success);
    for (const auto& frame : harmonics.document.keyframes)
        CHECK (frame.phases.empty());

    SampleSource noise;
    noise.sampleRate = rate;
    noise.frames = 24000;
    juce::Random random (3);
    for (int i = 0; i < noise.frames; ++i)
        noise.data.push_back (random.nextFloat() * 2.0f - 1.0f);
    CHECK_FALSE (wavetableExtraction::fromSample (noise, { .method = wavetableExtraction::Method::Cycles }).success); // no pitch
    CHECK (wavetableExtraction::suggestMethod (noise) == wavetableExtraction::Method::Texture);
    CHECK (wavetableExtraction::suggestMethod (source) == wavetableExtraction::Method::Cycles);

    const auto texture = wavetableExtraction::fromSample (noise, { .method = wavetableExtraction::Method::Texture, .frames = 4 });
    REQUIRE (texture.success);
    REQUIRE (texture.document.keyframes.size() == 4);
    // White noise: a broad spectrum, every frame with the same fixed phases.
    const auto& first = texture.document.keyframes.front();
    int heard = 0;
    for (const auto a : first.amplitudes)
        heard += a > 0.01f;
    CHECK (heard > 100);
    CHECK (first.phases == texture.document.keyframes.back().phases);
}

TEST_CASE ("A file of 2048-sample cycles becomes one keyframe per cycle", "[engine][wavetable][3a]")
{
    // Four frames: a sine, then square waves with 3, 5, 7 odd harmonics.
    SampleSource file;
    file.sampleRate = 44100.0;
    file.frames = 4 * 2048;
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i < 2048; ++i)
        {
            double v = 0.0;
            for (int h = 1; h <= 1 + 2 * k; h += 2)
                v += std::sin (juce::MathConstants<double>::twoPi * h * i / 2048.0) / h;
            file.data.push_back ((float) v);
        }
    REQUIRE (wavetableExtraction::cycleFileLength (file) == 2048);
    CHECK (wavetableExtraction::suggestMethod (file) == wavetableExtraction::Method::CycleFile);
    const auto result = wavetableExtraction::fromSample (file, { .method = wavetableExtraction::Method::CycleFile, .frames = 64 });
    REQUIRE (result.success);
    const auto& frames = result.document.keyframes;
    REQUIRE (frames.size() == 4);
    CHECK (ratio (frames[0], 3) < 1.0e-3f);
    CHECK (ratio (frames[3], 3) == Catch::Approx (1.0f / 3.0f).margin (1.0e-3));
    CHECK (ratio (frames[3], 7) == Catch::Approx (1.0f / 7.0f).margin (1.0e-3));
    CHECK (ratio (frames[3], 9) < 1.0e-3f);

    // A single short cycle (AKWF-style, 600 samples) is one keyframe.
    SampleSource single;
    single.sampleRate = 44100.0;
    single.frames = 600;
    for (int i = 0; i < 600; ++i)
        single.data.push_back ((float) std::sin (juce::MathConstants<double>::twoPi * i / 600.0));
    const auto one = wavetableExtraction::fromSample (single, { .method = wavetableExtraction::Method::CycleFile });
    REQUIRE (one.success);
    REQUIRE (one.document.keyframes.size() == 1);
    CHECK (one.document.keyframes[0].amplitudes[0] == Catch::Approx (1.0f).margin (0.01));
}

#include "bazalt/engine/RtAllocationTrap.h"
#include "bazalt/engine/nodes/DataWavetableNode.h"
#include "bazalt/engine/nodes/RecordNode.h"

TEST_CASE ("A wavetable captures a live recording: the audio thread copies, the message thread builds", "[engine][wavetable][3a]")
{
    nodes::RecordNode record;
    record.prepare ({ rate, 256 });
    record.setParameter ("data.record.length", 1.0f);
    record.setParameter ("data.record.channels", 0.0f);
    nodes::DataWavetableNode table;
    table.prepare ({ rate, 256 });
    table.setDataInput ("source", record.getDataPublisher());
    const auto* before = table.getDataPublisher()->getCurrentForAudioThread();
    REQUIRE (before != nullptr);
    const auto generationBefore = before->generation;

    std::vector<float> in (256), nan (256, std::numeric_limits<float>::quiet_NaN()), zero (256, 0.0f), capture (256, 0.0f);
    std::vector<float> o1 (256), o2 (256), o3 (256);
    for (int b = 0; b < 100; ++b) // ~0.5 s of a 330 Hz saw-ish tone
    {
        for (int s = 0; s < 256; ++s)
        {
            const auto t = b * 256 + s;
            in[(size_t) s] = 0.5f * (float) (std::sin (juce::MathConstants<double>::twoPi * 330.0 * t / rate)
                                             + 0.5 * std::sin (juce::MathConstants<double>::twoPi * 660.0 * t / rate));
        }
        std::fill (capture.begin(), capture.end(), 0.0f);
        if (b == 90)
            capture[100] = 1.0f;
        const float* ri[] = { in.data(), in.data(), nan.data(), zero.data(), nan.data() };
        float* ro[] = { o1.data(), o2.data(), o3.data() };
        const float* ti[] = { nan.data(), zero.data(), capture.data() };
        float* to[] = { o1.data() };
        {
            ScopedAudioThreadAllocationTrap trap;
            record.processBlock (ri, ro, 256);
            table.processBlock (ti, to, 256);
        }
    }
    CHECK (table.getCapturesTaken() == 0); // nothing until the message thread runs
    table.serviceOnMessageThread();
    CHECK (table.getCapturesTaken() == 1);
    const auto* after = table.getDataPublisher()->getCurrentForAudioThread();
    REQUIRE (after != nullptr);
    CHECK (after->generation > generationBefore);
    table.serviceOnMessageThread(); // nothing waiting: nothing changes
    CHECK (table.getCapturesTaken() == 1);
}
