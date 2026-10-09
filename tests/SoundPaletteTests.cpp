// wiki/plans/SoundPalette.md: one harness per family — noise spectra,
// shaper curves and aliasing, the LFO, level, dynamics, the frequency
// shifter — plus block-size invariance for every node the palette added.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/HostInputs.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/nodes/AnalysisLevelNode.h"
#include "bazalt/engine/nodes/DynamicsNodes.h"
#include "bazalt/engine/nodes/FreqShiftNode.h"
#include "bazalt/engine/nodes/LfoNode.h"
#include "bazalt/engine/nodes/NoiseColoredNode.h"
#include "bazalt/engine/nodes/NoiseDustNode.h"
#include "bazalt/engine/nodes/ShapeNodes.h"
#include <juce_dsp/juce_dsp.h>
#include <limits>
#include <set>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    constexpr double fs = 48000.0;
    using Signal = std::vector<float>;

    /** Runs a node over `inputs` (one per flat input channel; an empty
        signal means unconnected = NaN) and returns every flat output. */
    std::vector<Signal> run (Node& node, const std::vector<Signal>& inputs, int numSamples, int blockSize = 256)
    {
        const auto numIn = node.getNumInputChannels(), numOut = node.getNumOutputChannels();
        std::vector<Signal> outs ((size_t) numOut, Signal ((size_t) numSamples));
        const Signal nan ((size_t) blockSize, std::numeric_limits<float>::quiet_NaN());
        for (int start = 0; start < numSamples; start += blockSize)
        {
            const auto n = std::min (blockSize, numSamples - start);
            std::vector<const float*> in ((size_t) numIn, nan.data());
            for (size_t i = 0; i < inputs.size() && i < (size_t) numIn; ++i)
                if (! inputs[i].empty())
                    in[i] = inputs[i].data() + start;
            std::vector<float*> out ((size_t) numOut);
            for (size_t o = 0; o < (size_t) numOut; ++o)
                out[o] = outs[o].data() + start;
            node.processBlock (in.data(), out.data(), n);
        }
        return outs;
    }

    template <typename NodeType>
    std::unique_ptr<NodeType> make (std::initializer_list<std::pair<const char*, float>> parameters = {})
    {
        auto node = std::make_unique<NodeType>();
        node->prepare ({ fs, 512 });
        for (const auto& [id, value] : parameters)
            node->setParameter (id, value);
        return node;
    }

    Signal sine (double hz, double seconds, double amplitude = 1.0)
    {
        Signal s ((size_t) (seconds * fs));
        for (size_t i = 0; i < s.size(); ++i)
            s[i] = (float) (amplitude * std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / fs));
        return s;
    }

    Signal constant (float value, double seconds) { return Signal ((size_t) (seconds * fs), value); }

    /** Amplitude of the `hz` component (Goertzel), over the second half. */
    double amplitudeAt (const Signal& x, double hz)
    {
        const auto start = x.size() / 2, n = x.size() - start;
        const auto w = juce::MathConstants<double>::twoPi * hz / fs;
        double s1 = 0, s2 = 0;
        for (size_t i = start; i < x.size(); ++i)
        {
            const auto s = (double) x[i] + 2.0 * std::cos (w) * s1 - s2;
            s2 = s1;
            s1 = s;
        }
        return std::sqrt (s1 * s1 + s2 * s2 - 2.0 * std::cos (w) * s1 * s2) * 2.0 / (double) n;
    }

    /** Welch-averaged power in an octave band around `centre`, dB. */
    double octaveBandDb (const Signal& x, double centre)
    {
        constexpr int order = 12, size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<double> power (size / 2, 0.0);
        std::vector<float> buffer (size * 2);
        int frames = 0;
        for (size_t start = 0; start + size <= x.size(); start += size / 2, ++frames)
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            for (int i = 0; i < size; ++i)
                buffer[(size_t) i] = x[start + (size_t) i] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) size));
            fft.performFrequencyOnlyForwardTransform (buffer.data());
            for (int b = 0; b < size / 2; ++b)
                power[(size_t) b] += (double) buffer[(size_t) b] * buffer[(size_t) b];
        }
        const auto binHz = fs / size;
        // Mean power per bin (a density), not the band's sum — a sum grows
        // 3 dB per octave just because higher octaves hold more bins.
        double sum = 0;
        int bins = 0;
        for (auto b = (size_t) (centre / std::sqrt (2.0) / binHz); b <= (size_t) (centre * std::sqrt (2.0) / binHz); ++b, ++bins)
            sum += power[b];
        return 10.0 * std::log10 (sum / bins / frames + 1e-30);
    }

    /** Fraction of a periodic signal's energy that is NOT at a harmonic of
        `f0` (i.e. aliasing), dB. */
    double aliasingDb (const Signal& x, double f0)
    {
        constexpr int order = 15, size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> buffer (size * 2, 0.0f);
        const auto start = x.size() - size;
        for (int i = 0; i < size; ++i)
            buffer[(size_t) i] = x[start + (size_t) i] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) size));
        fft.performFrequencyOnlyForwardTransform (buffer.data());
        const auto binHz = fs / size;
        double harmonic = 0, alias = 0;
        for (int b = 2; b < size / 2; ++b)
        {
            const auto hz = b * binHz;
            const auto k = std::round (hz / f0);
            const auto p = (double) buffer[(size_t) b] * buffer[(size_t) b];
            (k >= 1 && std::abs (hz - k * f0) < 4.0 * binHz ? harmonic : alias) += p;
        }
        return 10.0 * std::log10 (alias / (harmonic + alias));
    }

    double rms (const Signal& x, size_t from = 0)
    {
        double s = 0;
        for (auto i = from; i < x.size(); ++i)
            s += (double) x[i] * x[i];
        return std::sqrt (s / (double) (x.size() - from));
    }
}

//==============================================================================
TEST_CASE ("noise.colored has the slope its colour names, at a matched level", "[engine][palette][noise]")
{
    const std::pair<int, double> colours[] = { { 0, 0.0 }, { 1, -3.0 }, { 2, -6.0 }, { 3, 3.0 }, { 4, 6.0 } };
    std::vector<double> levels;
    for (const auto& [colour, slope] : colours)
    {
        auto node = make<NoiseColoredNode> ({ { "source.noise.colour", (float) colour }, { "source.noise.level", 1.0f } });
        const auto out = run (*node, {}, (int) (8.0 * fs))[0];
        const auto low = octaveBandDb (out, 250.0), high = octaveBandDb (out, 4000.0);
        const auto measured = (high - low) / 4.0;
        WARN ("colour " << colour << ": " << measured << " dB/oct (want " << slope << "), rms " << rms (out));
        CHECK (measured == Catch::Approx (slope).margin (1.0));
        levels.push_back (20.0 * std::log10 (rms (out)));
    }
    const auto [lo, hi] = std::minmax_element (levels.begin(), levels.end());
    CHECK (*hi - *lo < 6.0); // switching colour changes the tone, not the level
}

TEST_CASE ("noise.colored stereo is decorrelated and seeds are reproducible", "[engine][palette][noise]")
{
    auto node = make<NoiseColoredNode> ({ { "source.noise.stereo", 1.0f }, { "source.noise.colour", 0.0f } });
    REQUIRE (node->getNumOutputChannels() == 2);
    const auto out = run (*node, {}, (int) fs);
    double lr = 0, ll = 0, rr = 0;
    for (size_t i = 0; i < out[0].size(); ++i)
    {
        lr += (double) out[0][i] * out[1][i];
        ll += (double) out[0][i] * out[0][i];
        rr += (double) out[1][i] * out[1][i];
    }
    CHECK (std::abs (lr / std::sqrt (ll * rr)) < 0.02);

    auto a = make<NoiseColoredNode> ({ { "source.noise.seed", 7.0f } }), b = make<NoiseColoredNode> ({ { "source.noise.seed", 7.0f } });
    auto c = make<NoiseColoredNode> ({ { "source.noise.seed", 8.0f } });
    CHECK (run (*a, {}, 1000)[0] == run (*b, {}, 1000)[0]);
    CHECK (run (*a, {}, 1000)[0] != run (*c, {}, 1000)[0]);
}

TEST_CASE ("noise.dust fires at its density, and trigger marks every impulse", "[engine][palette][noise]")
{
    auto node = make<NoiseDustNode> ({ { "source.dust.density", 200.0f } });
    const auto out = run (*node, {}, (int) (10.0 * fs));
    int impulses = 0, triggers = 0;
    for (size_t i = 0; i < out[0].size(); ++i)
    {
        impulses += out[0][i] != 0.0f ? 1 : 0;
        triggers += out[1][i] != 0.0f ? 1 : 0;
    }
    CHECK (impulses == Catch::Approx (2000).epsilon (0.1));
    CHECK (triggers == impulses);
}

//==============================================================================
TEST_CASE ("ADAA shapers alias much less than the bare curve", "[engine][palette][shape]")
{
    const auto f0 = 3517.0;
    const auto input = sine (f0, 1.0, 0.95);

    const auto naive = [&] (auto curve)
    {
        Signal out (input.size());
        for (size_t i = 0; i < input.size(); ++i)
            out[i] = (float) curve ((double) input[i]);
        return out;
    };

    auto hard = make<ShapeWaveshaperNode> ({ { "shape.waveshaper.curve", 2.0f }, { "shape.waveshaper.drive", 20.0f } });
    const auto hardOut = run (*hard, { input }, (int) input.size())[0];
    const auto hardNaive = naive ([] (double x) { return juce::jlimit (-1.0, 1.0, x * 10.0); });

    auto fold = make<ShapeFoldNode> ({ { "shape.fold.fold", 3.0f } });
    const auto foldOut = run (*fold, { input }, (int) input.size())[0];
    const auto foldNaive = naive ([] (double x) { return ShapeFoldNode::triangle (x * 3.0); });

    auto rectify = make<ShapeRectifyNode>();
    const auto rectifyOut = run (*rectify, { input }, (int) input.size())[0];
    const auto rectifyNaive = naive ([] (double x) { return std::abs (x); });

    struct Case { const char* name; double adaa, bare; };
    for (const auto& c : { Case { "hard clip", aliasingDb (hardOut, f0), aliasingDb (hardNaive, f0) },
                           Case { "fold", aliasingDb (foldOut, f0), aliasingDb (foldNaive, f0) },
                           Case { "rectify", aliasingDb (rectifyOut, f0), aliasingDb (rectifyNaive, f0) } })
    {
        // First-order ADAA buys ~5-8 dB at a 3.5 kHz fundamental; more would
        // need oversampling on top, not attempted here.
        WARN (c.name << ": aliasing " << c.adaa << " dB with ADAA, " << c.bare << " dB bare");
        CHECK (c.adaa < c.bare - 5.0);
    }
}

TEST_CASE ("shape.waveshaper follows its curve and removes the bias DC", "[engine][palette][shape]")
{
    for (int curve = 0; curve < 6; ++curve)
    {
        auto node = make<ShapeWaveshaperNode> ({ { "shape.waveshaper.curve", (float) curve }, { "shape.waveshaper.drive", 0.0f } });
        const auto slow = sine (2.0, 0.5, 0.9); // slow enough that ADAA ≈ the curve itself
        const auto out = run (*node, { slow }, (int) slow.size())[0];
        for (size_t i = 100; i < slow.size(); i += 997)
            CHECK (out[i] == Catch::Approx (ShapeWaveshaperNode::shape ((ShapeWaveshaperNode::Curve) curve, slow[i])).margin (2.0e-3));
    }

    auto biased = make<ShapeWaveshaperNode> ({ { "shape.waveshaper.bias", 0.5f } });
    CHECK (std::abs (run (*biased, { constant (0.0f, 0.1) }, 4800)[0].back()) < 1.0e-6f); // silence stays silence
}

TEST_CASE ("shape.fold folds back past ±1", "[engine][palette][shape]")
{
    auto node = make<ShapeFoldNode> ({ { "shape.fold.fold", 2.0f } });
    const auto out = run (*node, { constant (0.75f, 0.05) }, 2400)[0];
    CHECK (out.back() == Catch::Approx (0.5f).margin (1.0e-4)); // 1.5 folds to 0.5
    CHECK (ShapeFoldNode::triangle (-1.0) == Catch::Approx (-1.0));
    CHECK (ShapeFoldNode::triangle (3.0) == Catch::Approx (-1.0));
}

TEST_CASE ("shape.crush quantises to its bit depth and holds at its rate", "[engine][palette][shape]")
{
    auto node = make<ShapeCrushNode> ({ { "shape.crush.bits", 3.0f }, { "shape.crush.rate", 1000.0f } });
    const auto out = run (*node, { sine (101.0, 0.5, 0.99) }, (int) (0.5 * fs))[0];
    std::set<float> values (out.begin(), out.end());
    CHECK (values.size() <= 9); // 2^3 steps around zero, plus the end
    int longestRun = 0, runLength = 1, changes = 0;
    for (size_t i = 1; i < out.size(); ++i)
    {
        runLength = out[i] == out[i - 1] ? runLength + 1 : 1;
        changes += out[i] != out[i - 1] ? 1 : 0;
        longestRun = std::max (longestRun, runLength);
    }
    CHECK (changes <= 501); // at most one new value per 1 ms
}

//==============================================================================
TEST_CASE ("lfo.shape runs at its rate, in its polarity", "[engine][palette][lfo]")
{
    auto node = make<LfoNode> ({ { "lfo.shape.rate", 2.0f } });
    const auto out = run (*node, {}, (int) fs)[0];
    CHECK (out[0] == Catch::Approx (0.0f).margin (1e-6));
    CHECK (out[6000] == Catch::Approx (1.0f).margin (1e-4)); // a quarter cycle at 2 Hz
    CHECK (out[24000] == Catch::Approx (0.0f).margin (1e-3)); // a full cycle

    auto unipolar = make<LfoNode> ({ { "lfo.shape.polarity", 1.0f }, { "lfo.shape.shape", 4.0f } });
    const auto square = run (*unipolar, {}, (int) fs)[0];
    CHECK (*std::min_element (square.begin(), square.end()) == 0.0f);
    CHECK (*std::max_element (square.begin(), square.end()) == 1.0f);

    auto held = make<LfoNode> ({ { "lfo.shape.shape", 5.0f }, { "lfo.shape.rate", 10.0f } });
    const auto sh = run (*held, {}, (int) fs)[0];
    std::set<float> distinct (sh.begin(), sh.end());
    CHECK (distinct.size() >= 9);
    CHECK (distinct.size() <= 11); // one value per cycle
}

TEST_CASE ("lfo.shape synced to a playing host is locked to the timeline", "[engine][palette][lfo]")
{
    auto node = make<LfoNode> ({ { "lfo.shape.sync", 1.0f }, { "lfo.shape.division", 5.0f }, { "lfo.shape.shape", 2.0f } }); // 1/4 = 1 beat, saw
    HostInputs host;
    host.transportPlaying = true;
    host.tempoBpm = 120.0;
    host.ppqPosition = 10.25; // a quarter into a beat
    node->setHostInputs (host);
    const auto out = run (*node, {}, 1)[0];
    CHECK (out[0] == Catch::Approx (2.0 * 0.25 - 1.0).margin (1e-4)); // saw at phase 0.25
}

TEST_CASE ("analysis.level reads RMS and peak, in dB too", "[engine][palette][analysis]")
{
    auto node = make<AnalysisLevelNode>();
    const auto s = sine (1000.0, 1.0);
    const auto out = run (*node, { s, s }, (int) s.size());
    CHECK (out[0].back() == Catch::Approx (0.7071).margin (0.01));
    CHECK (out[1].back() == Catch::Approx (-3.01).margin (0.1));

    auto peak = make<AnalysisLevelNode> ({ { "analysis.level.mode", 1.0f }, { "analysis.level.attack", 0.0f } });
    const auto peakOut = run (*peak, { s, s }, (int) s.size());
    CHECK (peakOut[0].back() == Catch::Approx (1.0).margin (0.02));
}

//==============================================================================
TEST_CASE ("dyn.compress follows its static curve, and the sidechain ducks", "[engine][palette][dynamics]")
{
    // A full-scale tone, threshold -20, ratio 4, hard knee: 20 dB over -> 5 dB over -> 15 dB of reduction.
    auto node = make<DynCompressNode> ({ { "dynamics.compress.threshold", -20.0f }, { "dynamics.compress.ratio", 4.0f }, { "dynamics.compress.knee", 0.0f } });
    const auto s = sine (500.0, 1.0);
    const auto out = run (*node, { s, s }, (int) s.size());
    CHECK (out[3].back() == Catch::Approx (15.0).margin (0.5));
    CHECK (out[2].back() == Catch::Approx (std::pow (10.0, -15.0 / 20.0)).margin (0.01));

    CHECK (DynCompressNode::curve (-10.0, -20.0, 4.0, 10.0) == Catch::Approx (-17.5));
    CHECK (DynCompressNode::curve (-40.0, -20.0, 4.0, 10.0) == Catch::Approx (-40.0));

    auto ducker = make<DynCompressNode> ({ { "dynamics.compress.threshold", -30.0f }, { "dynamics.compress.ratio", 20.0f } });
    const auto quiet = sine (300.0, 1.0, 0.1);
    const auto loudKey = sine (80.0, 1.0, 1.0);
    const auto ducked = run (*ducker, { quiet, quiet, loudKey, loudKey }, (int) quiet.size());
    CHECK (rms (ducked[0], ducked[0].size() / 2) < rms (quiet) * 0.2);
}

TEST_CASE ("dyn.gate opens above its threshold and closes by its range", "[engine][palette][dynamics]")
{
    auto node = make<DynGateNode> ({ { "dynamics.gate.threshold", -30.0f }, { "dynamics.gate.range", 40.0f } });
    const auto loud = sine (500.0, 0.5, 0.5);
    const auto open = run (*node, { loud, loud }, (int) loud.size());
    CHECK (open[3].back() == 1.0f);
    CHECK (rms (open[0], open[0].size() / 2) == Catch::Approx (rms (loud)).epsilon (0.02));

    auto closedGate = make<DynGateNode> ({ { "dynamics.gate.threshold", -30.0f }, { "dynamics.gate.range", 40.0f } });
    const auto soft = sine (500.0, 0.5, 0.01); // -40 dB
    const auto closed = run (*closedGate, { soft, soft }, (int) soft.size());
    CHECK (closed[3].back() == 0.0f);
    CHECK (rms (closed[0], closed[0].size() / 2) == Catch::Approx (rms (soft) * 0.01).epsilon (0.05));
}

TEST_CASE ("fx.freqShift moves a tone by Hz, one sideband at a time", "[engine][palette][freqShift]")
{
    auto node = make<FreqShiftNode> ({ { "spectrum.freqShift.shift", 100.0f } });
    const auto out = run (*node, { sine (1000.0, 1.0, 0.5) }, (int) fs);
    const auto upWanted = amplitudeAt (out[0], 1100.0), upUnwanted = amplitudeAt (out[0], 900.0);
    const auto downWanted = amplitudeAt (out[1], 900.0), downUnwanted = amplitudeAt (out[1], 1100.0);
    WARN ("up: 1100 Hz " << upWanted << ", 900 Hz " << upUnwanted << "; down: 900 Hz " << downWanted << ", 1100 Hz " << downUnwanted);
    CHECK (upWanted == Catch::Approx (0.5).epsilon (0.05));
    CHECK (20.0 * std::log10 (upWanted / upUnwanted) > 35.0);
    CHECK (20.0 * std::log10 (downWanted / downUnwanted) > 35.0);
}

//==============================================================================
TEST_CASE ("Every Sound Palette node is block-size invariant", "[engine][palette]")
{
    auto factory = buildDefaultNodeFactory();
    for (const auto* type : { "source.noise", "source.dust", "shape.rectify", "shape.crush", "shape.waveshaper", "shape.fold",
                              "lfo.shape", "analysis.level", "dynamics.compress", "dynamics.gate", "spectrum.freqShift",
                              "filter.svf", "osc.analog", "excite.burst" })
    {
        const auto render = [&] (int blockSize)
        {
            auto node = factory.create (type);
            node->prepare ({ fs, 512 });
            const auto s = sine (440.0, 0.25, 0.8);
            std::vector<Signal> inputs { s, s };
            return run (*node, inputs, (int) s.size(), blockSize);
        };
        INFO (type);
        const auto a = render (1), b = render (64), c = render (509);
        CHECK (a == b);
        CHECK (a == c);
    }
}

//==============================================================================
// Batch 2: the three MVPs closed.
#include "bazalt/engine/nodes/NoiseBurstNode.h"
#include "bazalt/engine/nodes/OscillatorNode.h"
#include "bazalt/engine/nodes/SvfFilterNode.h"

TEST_CASE ("filter.svf gives all five responses at once", "[engine][palette][svf]")
{
    const auto respond = [] (double hz)
    {
        auto node = make<SvfFilterNode> ({ { "filter.svf.cutoff", 1000.0f }, { "filter.svf.resonance", 0.7071f } });
        const auto out = run (*node, { sine (hz, 0.5) }, (int) (0.5 * fs));
        std::array<double, 5> amplitudes {};
        for (size_t o = 0; o < 5; ++o)
            amplitudes[o] = amplitudeAt (out[o], hz);
        return amplitudes;
    };
    const auto low = respond (100.0), at = respond (1000.0), high = respond (10000.0);
    // lowpass, bandpass, highpass, notch, peak
    CHECK (low[0] == Catch::Approx (1.0).margin (0.02));
    CHECK (high[0] < 0.02);
    CHECK (high[2] == Catch::Approx (1.0).margin (0.02));
    CHECK (low[2] < 0.02);
    CHECK (at[0] == Catch::Approx (0.7071).margin (0.02)); // -3 dB at cutoff, Q = 0.707
    CHECK (at[1] == Catch::Approx (0.7071).margin (0.02)); // this form's bandpass peaks at Q (constant-skirt)
    CHECK (at[3] < 0.02);                                          // notch nulls the cutoff
    CHECK (low[3] == Catch::Approx (1.0).margin (0.02));
}

TEST_CASE ("osc.analog: fine tune, pulse width and sync", "[engine][palette][osc]")
{
    auto tuned = make<OscillatorNode> ({ { "osc.analog.shape", 0.0f }, { "osc.analog.frequency", 1000.0f }, { "osc.analog.fine", 100.0f } });
    const auto out = run (*tuned, {}, (int) fs)[0];
    CHECK (amplitudeAt (out, 1000.0 * std::exp2 (100.0 / 1200.0)) == Catch::Approx (1.0).margin (0.02)); // +100 cents = a semitone up

    auto pulse = make<OscillatorNode> ({ { "osc.analog.shape", 2.0f }, { "osc.analog.frequency", 100.0f }, { "osc.analog.pulseWidth", 0.25f } });
    const auto square = run (*pulse, {}, (int) fs)[0];
    const auto high = std::count_if (square.begin(), square.end(), [] (float v) { return v > 0.0f; });
    CHECK ((double) high / (double) square.size() == Catch::Approx (0.25).margin (0.01));

    auto synced = make<OscillatorNode> ({ { "osc.analog.shape", 1.0f }, { "osc.analog.frequency", 100.0f } });
    Signal sync ((size_t) 1000, 0.0f);
    sync[500] = 1.0f;
    const auto saw = run (*synced, { {}, {}, {}, {}, {}, sync }, 1000)[0];
    auto fresh = make<OscillatorNode> ({ { "osc.analog.shape", 1.0f }, { "osc.analog.frequency", 100.0f } });
    const auto start = run (*fresh, {}, 1)[0];
    CHECK (saw[500] == start[0]); // sync restarts the cycle
}

TEST_CASE ("excite.burst: tone tilts the spectrum, shape bends the decay", "[engine][palette][burst]")
{
    const auto burst = [] (float tone, float shape)
    {
        NoiseBurstNode node;
        node.prepare ({ fs, 512 });
        node.setParameter ("excite.burst.tone", tone);
        node.setParameter ("excite.burst.shape", shape);
        node.setParameter ("excite.burst.duration", 200.0f);
        Signal trigger ((size_t) (0.3 * fs), 0.0f);
        trigger[0] = 1.0f;
        return run (node, { trigger }, (int) trigger.size())[0];
    };
    const auto dark = burst (-1.0f, 0.0f), bright = burst (1.0f, 0.0f);
    CHECK (octaveBandDb (dark, 250.0) - octaveBandDb (dark, 8000.0) > octaveBandDb (bright, 250.0) - octaveBandDb (bright, 8000.0) + 20.0);

    const auto fast = burst (0.0f, -1.0f), held = burst (0.0f, 1.0f);
    CHECK (rms (fast) < rms (held) * 0.6);
}
