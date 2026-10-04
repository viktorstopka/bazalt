// space.reverb / space.diffuser held to the measured quality bar of
// wiki/plans/Reverb.md §5: decay time per band, echo density, no metallic
// ringing, stereo decorrelation, mono compatibility, freeze stability,
// stability at the extremes, block-size invariance. The measured values are
// printed (WARN) so a tuning pass can read them; the bars are the CHECKs.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "ReverbMeasurements.h"
#include "bazalt/engine/nodes/DiffuserNode.h"
#include "bazalt/engine/nodes/ReverbNode.h"
#include <chrono>

using namespace reverbtest;
using bazalt::engine::NodePrepareInfo;
using bazalt::engine::nodes::DiffuserNode;
using bazalt::engine::nodes::ReverbNode;

namespace
{
    constexpr double fs = 48000.0;

    struct Settings
    {
        float decay = 2.0f, decayLow = 1.0f, decayHigh = 1.0f, size = 12.0f;
        float diffusion = 0.85f, modulation = 0.3f, width = 1.0f, early = 0.0f;
        bool sixteenLines = false;
    };

    std::unique_ptr<ReverbNode> makeReverb (const Settings& s)
    {
        auto node = std::make_unique<ReverbNode>();
        node->prepare (NodePrepareInfo { fs, 256 });
        node->setParameter ("space.reverb.quality", s.sixteenLines ? 1.0f : 0.0f);
        node->setParameter ("space.reverb.decay", s.decay);
        node->setParameter ("space.reverb.decayLow", s.decayLow);
        node->setParameter ("space.reverb.decayHigh", s.decayHigh);
        node->setParameter ("space.reverb.size", s.size);
        node->setParameter ("space.reverb.diffusion", s.diffusion);
        node->setParameter ("space.reverb.modulation", s.modulation);
        node->setParameter ("space.reverb.width", s.width);
        node->setParameter ("space.reverb.early", s.early);
        node->setParameter ("space.reverb.mix", 1.0f);
        node->setParameter ("space.reverb.predelay", 0.0f);
        node->setParameter ("space.reverb.lowCut", 20.0f);
        node->setParameter ("space.reverb.highCut", 20000.0f);
        return node;
    }

    Stereo impulseResponse (const Settings& s, double seconds)
    {
        auto node = makeReverb (s);
        return render (*node, impulse (seconds, fs));
    }

    Stereo noise (double seconds, juce::int64 seed, double gateSeconds = 1.0e9)
    {
        juce::Random random (seed);
        Stereo in { std::vector<float> ((size_t) (seconds * fs)), std::vector<float> ((size_t) (seconds * fs)) };
        for (size_t i = 0; i < in.left.size(); ++i)
        {
            const auto on = (double) i / fs < gateSeconds;
            in.left[i] = on ? random.nextFloat() * 2.0f - 1.0f : 0.0f;
            in.right[i] = on ? random.nextFloat() * 2.0f - 1.0f : 0.0f;
        }
        return in;
    }
}

TEST_CASE ("space.reverb's measured decay time matches the decay control", "[engine][reverb]")
{
    for (const auto lines16 : { false, true })
        for (const auto decay : { 0.3f, 1.0f, 2.5f, 8.0f, 20.0f })
        {
            Settings s;
            s.decay = decay;
            s.sixteenLines = lines16;
            const auto ir = impulseResponse (s, std::max (1.0, decay * 1.4));
            const auto measured = rt60 (band (ir.left, fs, 1000.0), fs);
            WARN ("lines " << (lines16 ? 16 : 8) << " decay " << decay << " s -> measured " << measured << " s @1k");
            CHECK (measured == Catch::Approx (decay).epsilon (0.10));
        }
}

TEST_CASE ("space.reverb's per-band decay follows decayLow / decayHigh", "[engine][reverb]")
{
    Settings s;
    s.decay = 2.0f;
    s.decayLow = 2.0f;
    s.decayHigh = 0.4f;
    const auto ir = impulseResponse (s, 6.0);
    const auto low = rt60 (band (ir.left, fs, 63.0), fs);
    const auto mid = rt60 (band (ir.left, fs, 1000.0), fs);
    const auto high = rt60 (band (ir.left, fs, 12000.0), fs);
    WARN ("bands: 63 Hz " << low << " s (want 4.0), 1 kHz " << mid << " s (want 2.0), 12 kHz " << high << " s (want 0.8)");
    CHECK (low == Catch::Approx (4.0).epsilon (0.10));
    CHECK (mid == Catch::Approx (2.0).epsilon (0.10));
    CHECK (high == Catch::Approx (0.8).epsilon (0.10));
}

TEST_CASE ("space.reverb gets dense quickly", "[engine][reverb]")
{
    for (const auto size : { 5.0f, 12.0f, 30.0f })
    {
        Settings s;
        s.size = size;
        s.diffusion = 1.0f;
        const auto ir = impulseResponse (s, 1.0);
        const auto t = echoDensityTime (ir.left, fs);
        WARN ("size " << size << " m: echo density reaches 0.9 at " << t * 1000.0 << " ms");
        CHECK (t > 0.0);
        CHECK (t < 0.15);
    }
}

TEST_CASE ("space.reverb's tail doesn't ring at fixed frequencies", "[engine][reverb]")
{
    // Exponentially decaying white noise measures ~11 dB here (see the hidden
    // "ringing exploration" case). With lines spanning only 0.5-1x the room's
    // crossing time the 8-line network measured 24-25 dB — sparse low modes
    // standing out — which is what moved the lines to 1-2x.
    for (const auto lines16 : { false, true })
    for (const auto modulation : { 0.0f, 0.3f })
    {
        Settings s;
        s.decay = 3.0f;
        s.modulation = modulation;
        s.sixteenLines = lines16;
        const auto ir = impulseResponse (s, 2.0);
        double peakHz = 0;
        const auto ringing = peakToLocalMedianDb (ir.left, fs, 0.3, 0.68, &peakHz);
        WARN ((lines16 ? 16 : 8) << " lines, modulation " << modulation << ": tail peak-to-local-median " << ringing << " dB at " << peakHz << " Hz");
        CHECK (ringing < 18.0);
    }
}

TEST_CASE ("space.reverb is wide and mono-compatible, and width 0 is mono", "[engine][reverb]")
{
    Settings s;
    const auto wide = impulseResponse (s, 1.5);
    const auto rho = correlation (wide, fs, 0.05, 1.0);
    const auto mono = monoSumDb (wide);
    WARN ("width 1: L/R correlation " << rho << ", mono sum " << mono << " dB");
    CHECK (std::abs (rho) < 0.3);
    CHECK (mono > -4.0);

    s.width = 0.0f;
    const auto narrow = impulseResponse (s, 1.5);
    CHECK (correlation (narrow, fs, 0.05, 1.0) > 0.999);
}

TEST_CASE ("space.reverb's freeze holds its level", "[engine][reverb]")
{
    Settings s;
    auto node = makeReverb (s);
    const auto excitation = noise (30.0, 7, 0.5);
    // Freeze 0.4 s in, while the noise is still playing.
    auto first = render (*node, Stereo { { excitation.left.begin(), excitation.left.begin() + (long) (0.4 * fs) },
                                         { excitation.right.begin(), excitation.right.begin() + (long) (0.4 * fs) } });
    node->setParameter ("space.reverb.freeze", 1.0f);
    const auto rest = render (*node, Stereo { { excitation.left.begin() + (long) (0.4 * fs), excitation.left.end() },
                                              { excitation.right.begin() + (long) (0.4 * fs), excitation.right.end() } });
    const auto early = rmsDb (rest.left, fs, 4.0, 5.0);
    const auto late = rmsDb (rest.left, fs, 28.0, 29.0);
    WARN ("freeze: " << early << " dB at 4-5 s, " << late << " dB at 28-29 s");
    CHECK (early > -40.0);
    CHECK (std::abs (late - early) < 0.5);
}

TEST_CASE ("space.reverb stays bounded at its extremes", "[engine][reverb]")
{
    for (const auto extreme : { 0, 1 })
    {
        Settings s;
        s.decay = 60.0f;
        s.decayLow = 4.0f;
        s.decayHigh = 2.0f;
        s.size = extreme == 0 ? 1.0f : 50.0f;
        s.modulation = 1.0f;
        s.sixteenLines = extreme == 1;
        auto node = makeReverb (s);
        node->setParameter ("space.reverb.modRate", 5.0f);
        const auto out = render (*node, noise (60.0, 11));
        auto peak = 0.0f;
        auto finite = true;
        for (size_t i = 0; i < out.left.size(); ++i)
        {
            finite = finite && std::isfinite (out.left[i]) && std::isfinite (out.right[i]);
            peak = std::max ({ peak, std::abs (out.left[i]), std::abs (out.right[i]) });
        }
        WARN ("extreme " << extreme << ": peak " << peak);
        CHECK (finite);
        CHECK (peak < 8.0f);
    }
}

TEST_CASE ("space.reverb and space.diffuser don't depend on block size", "[engine][reverb]")
{
    const auto in = noise (0.5, 3, 0.1);
    const auto run = [&] (int blockSize, bool reverbNode)
    {
        std::unique_ptr<bazalt::engine::Node> node;
        if (reverbNode)
            node = makeReverb ({});
        else
        {
            node = std::make_unique<DiffuserNode>();
            node->prepare (NodePrepareInfo { fs, 512 });
        }
        return render (*node, in, blockSize);
    };
    for (const auto reverbNode : { true, false })
    {
        const auto a = run (1, reverbNode), b = run (64, reverbNode), c = run (509, reverbNode);
        CHECK (a.left == b.left);
        CHECK (a.right == c.right);
    }
}

TEST_CASE ("space.diffuser smears without adding a tail and keeps energy", "[engine][reverb]")
{
    DiffuserNode node;
    node.prepare (NodePrepareInfo { fs, 256 });
    const auto out = render (node, impulse (0.3, fs));
    double energy = 0, after = 0;
    for (size_t i = 0; i < out.left.size(); ++i)
    {
        const auto e = (double) out.left[i] * out.left[i] + (double) out.right[i] * out.right[i];
        energy += e;
        if ((double) i / fs > 0.05) // 30 ms span — nothing left well after it
            after += e;
    }
    WARN ("diffuser energy " << energy << " (input 2.0), after 50 ms " << after);
    // Lossless inside; the stereo read-out of 8 channels only preserves energy
    // on average, so one impulse lands within a few percent.
    CHECK (energy == Catch::Approx (2.0).epsilon (0.05));
    CHECK (after < 1.0e-9);
    CHECK (echoDensityTime (out.left, fs, 0.5) > 0.0);
}

TEST_CASE ("space.reverb CPU cost", "[engine][reverb][benchmark]")
{
    for (const auto lines16 : { false, true })
    {
        Settings s;
        s.sixteenLines = lines16;
        auto node = makeReverb (s);
        const auto in = noise (10.0, 5);
        const auto start = std::chrono::steady_clock::now();
        render (*node, in, 256);
        const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        WARN ((lines16 ? 16 : 8) << " lines: 10 s of stereo audio in " << seconds * 1000.0 << " ms (" << seconds * 10.0 << "% of one core, Debug)");
        CHECK (seconds < 10.0);
    }
}

TEST_CASE ("ringing exploration", "[.explore]")
{
    // Baseline: exponentially decaying white noise has no modes at all.
    juce::Random random (1);
    std::vector<float> noiseTail ((size_t) (2.0 * fs));
    for (size_t i = 0; i < noiseTail.size(); ++i)
        noiseTail[i] = (random.nextFloat() * 2.0f - 1.0f) * std::pow (10.0f, -3.0f * (float) i / (float) (3.0 * fs));
    double hz = 0;
    WARN ("decaying white noise: " << peakToLocalMedianDb (noiseTail, fs, 0.3, 0.68, &hz) << " dB at " << hz);

    for (const auto lines16 : { false, true })
        for (const auto modulation : { 0.0f, 0.3f, 0.7f, 1.0f })
        {
            Settings s;
            s.decay = 3.0f;
            s.modulation = modulation;
            s.sixteenLines = lines16;
            const auto ir = impulseResponse (s, 2.0);
            WARN ((lines16 ? 16 : 8) << " lines, mod " << modulation << ": " << peakToLocalMedianDb (ir.left, fs, 0.3, 0.68, &hz) << " dB at " << hz);
        }
}

// Listening renders for the tuning pass (wiki/plans/Reverb.md §4 step 5):
// BAZALT_RENDER_DIR=<dir> EngineTests "[.render]" writes one 32-bit float
// WAV per preset — a click, a snare-like burst, a sustained chord and a
// voice-like pulse source, in that order, each followed by room to decay.
namespace
{
    void writeWav (const juce::File& file, const Stereo& audio, double sampleRate)
    {
        juce::MemoryOutputStream out;
        const auto frames = (juce::uint32) audio.left.size();
        const auto dataBytes = frames * 2u * 4u;
        out.write ("RIFF", 4);
        out.writeInt ((int) (36 + dataBytes));
        out.write ("WAVEfmt ", 8);
        out.writeInt (16);
        out.writeShort (3); // IEEE float
        out.writeShort (2);
        out.writeInt ((int) sampleRate);
        out.writeInt ((int) sampleRate * 8);
        out.writeShort (8);
        out.writeShort (32);
        out.write ("data", 4);
        out.writeInt ((int) dataBytes);
        for (size_t i = 0; i < audio.left.size(); ++i)
        {
            out.writeFloat (audio.left[i]);
            out.writeFloat (audio.right[i]);
        }
        file.replaceWithData (out.getData(), out.getDataSize());
    }

    Stereo listeningSource()
    {
        const auto length = (size_t) (18.0 * fs);
        Stereo in { std::vector<float> (length), std::vector<float> (length) };
        juce::Random random (42);
        const auto at = [] (double seconds) { return (size_t) (seconds * fs); };

        in.left[at (0.0)] = in.right[at (0.0)] = 0.9f; // click

        for (size_t i = 0; i < at (0.25); ++i) // snare-like burst: noise, fast decay
        {
            const auto envelope = std::exp (-(float) i / (float) (0.04 * fs));
            const auto v = (random.nextFloat() * 2.0f - 1.0f) * envelope * 0.6f
                           + std::sin (juce::MathConstants<float>::twoPi * 190.0f * (float) i / (float) fs) * envelope * 0.4f;
            in.left[at (3.0) + i] += v;
            in.right[at (3.0) + i] += v;
        }

        for (const auto hz : { 220.0, 277.18, 329.63, 415.30 }) // a sustained chord, 2 s, gently filtered saws
        {
            auto phase = 0.0, low = 0.0;
            for (size_t i = 0; i < at (2.0); ++i)
            {
                phase += hz / fs;
                phase -= std::floor (phase);
                low += 0.15 * ((2.0 * phase - 1.0) - low);
                const auto fade = std::min (1.0, std::min ((double) i, (double) (at (2.0) - i)) / (0.01 * fs));
                in.left[at (6.0) + i] += (float) (low * 0.12 * fade);
                in.right[at (6.0) + i] += (float) (low * 0.12 * fade);
            }
        }

        // A voice-like source: a 140 Hz pulse train through two resonances.
        double b1 = 0, b2 = 0, c1 = 0, c2 = 0;
        const auto resonator = [] (double x, double& s1, double& s2, double hz, double q)
        {
            const auto w = juce::MathConstants<double>::twoPi * hz / fs;
            const auto r = std::exp (-w / (2.0 * q));
            const auto y = x + 2.0 * r * std::cos (w) * s1 - r * r * s2;
            s2 = s1;
            s1 = y;
            return y * (1.0 - r);
        };
        for (size_t i = 0; i < at (1.6); ++i)
        {
            const auto pulse = (i % (size_t) (fs / 140.0)) == 0 ? 1.0 : 0.0;
            const auto fade = std::min (1.0, std::min ((double) i, (double) (at (1.6) - i)) / (0.03 * fs));
            const auto v = (resonator (pulse, b1, b2, 700.0, 8.0) + 0.6 * resonator (pulse, c1, c2, 1200.0, 10.0)) * 0.9 * fade;
            in.left[at (11.0) + i] += (float) v;
            in.right[at (11.0) + i] += (float) v;
        }
        return in;
    }
}

TEST_CASE ("space.reverb listening renders", "[.render]")
{
    const auto dir = juce::File (juce::SystemStats::getEnvironmentVariable ("BAZALT_RENDER_DIR", juce::File::getCurrentWorkingDirectory().getFullPathName()));
    dir.createDirectory();

    struct Preset
    {
        const char* name;
        Settings settings;
        float mix;
    };
    const Preset presets[] = {
        { "reverb-room", { .decay = 0.8f, .decayLow = 1.1f, .decayHigh = 0.6f, .size = 6.0f, .diffusion = 0.9f, .modulation = 0.2f, .early = 0.5f }, 0.35f },
        { "reverb-hall", { .decay = 3.5f, .decayLow = 1.3f, .decayHigh = 0.5f, .size = 25.0f, .diffusion = 0.85f, .modulation = 0.35f, .early = 0.3f }, 0.35f },
        { "reverb-plate", { .decay = 2.2f, .decayLow = 0.8f, .decayHigh = 0.9f, .size = 8.0f, .diffusion = 1.0f, .modulation = 0.4f, .early = 0.0f, .sixteenLines = true }, 0.35f },
        { "reverb-huge", { .decay = 12.0f, .decayLow = 1.2f, .decayHigh = 0.45f, .size = 50.0f, .diffusion = 1.0f, .modulation = 0.6f, .early = 0.2f, .sixteenLines = true }, 0.4f },
    };

    const auto source = listeningSource();
    writeWav (dir.getChildFile ("reverb-dry.wav"), source, fs);
    for (const auto& preset : presets)
    {
        auto node = makeReverb (preset.settings);
        node->setParameter ("space.reverb.mix", preset.mix);
        node->setParameter ("space.reverb.predelay", 12.0f);
        node->setParameter ("space.reverb.highCut", 14000.0f);
        writeWav (dir.getChildFile (juce::String (preset.name) + ".wav"), render (*node, source), fs);
    }
}
