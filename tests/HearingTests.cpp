// wiki/plans/GuitarInput.md: hearing a played instrument — pitch, onsets,
// notes from audio, and a tracked phase — on guitar-like plucks.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/nodes/AnalysisHearingNodes.h"
#include "bazalt/engine/nodes/CurvePlayerNode.h"
#include <cmath>
#include <complex>
#include <limits>
#include <vector>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int block = 64;

    /** A plucked string: a few harmonics (the 2nd stronger than the
        fundamental, which is what fools naive trackers into the octave above),
        decaying. */
    void addPluck (std::vector<float>& signal, double startSeconds, double hz, double amplitude = 0.5)
    {
        const auto start = (size_t) (startSeconds * sampleRate);
        constexpr double weights[] = { 0.6, 0.9, 0.35, 0.2, 0.1 };
        for (size_t i = start; i < signal.size(); ++i)
        {
            const auto t = (double) (i - start) / sampleRate;
            double v = 0.0;
            for (int h = 0; h < 5; ++h)
                v += weights[h] * std::sin (juce::MathConstants<double>::twoPi * hz * (h + 1) * t);
            signal[i] += (float) (amplitude * 0.5 * v * std::exp (-t * 3.0));
        }
    }

    /** Runs a node over `input` in blocks; returns every output channel. */
    std::vector<std::vector<float>> run (Node& node, const std::vector<float>& input, int numOutputs)
    {
        std::vector<std::vector<float>> out ((size_t) numOutputs, std::vector<float> (input.size()));
        std::vector<std::vector<float>> scratch ((size_t) numOutputs, std::vector<float> (block));
        for (size_t start = 0; start < input.size(); start += block)
        {
            const auto n = (int) std::min ((size_t) block, input.size() - start);
            const float* in[1] = { input.data() + start };
            std::vector<float*> outs;
            for (auto& s : scratch)
                outs.push_back (s.data());
            node.processBlock (in, outs.data(), n);
            for (int c = 0; c < numOutputs; ++c)
                std::copy (scratch[(size_t) c].begin(), scratch[(size_t) c].begin() + n, out[(size_t) c].begin() + (long) start);
        }
        return out;
    }

    std::vector<NoteEvent> runNotes (AnalysisNotesNode& node, const std::vector<float>& input)
    {
        std::vector<NoteEvent> out (input.size());
        float dummy[block];
        for (size_t start = 0; start < input.size(); start += block)
        {
            const auto n = (int) std::min ((size_t) block, input.size() - start);
            const float* in[1] = { input.data() + start };
            float* outs[1] = { dummy };
            node.processBlock (in, outs, n);
            node.produceNoteBlock (out.data() + start, n);
        }
        return out;
    }

    double magnitudeAt (const std::vector<float>& signal, double frequency)
    {
        std::complex<double> sum;
        for (size_t i = 0; i < signal.size(); ++i)
            sum += (double) signal[i] * std::polar (1.0, -juce::MathConstants<double>::twoPi * frequency * (double) i / sampleRate);
        return std::abs (sum) / (double) signal.size();
    }
}

TEST_CASE ("Pitch finds the fundamental of a plucked note, not its louder octave", "[engine][hearing][pitch]")
{
    for (const auto [hz, expected] : { std::pair { 82.41, 40.0 }, std::pair { 329.63, 64.0 }, std::pair { 659.26, 76.0 } })
    {
        AnalysisPitchNode pitch;
        pitch.prepare ({ sampleRate, block });
        std::vector<float> signal ((size_t) (0.3 * sampleRate), 0.0f);
        addPluck (signal, 0.0, hz);
        const auto out = run (pitch, signal, 3);
        INFO (hz);
        const auto at = (size_t) (0.2 * sampleRate);
        CHECK (out[0][at] == Catch::Approx (expected).margin (0.1));
        CHECK (out[1][at] == Catch::Approx (hz).epsilon (0.01));
        CHECK (out[2][at] > 0.8f);
    }
}

TEST_CASE ("Pitch on one string's range answers within about two periods", "[engine][hearing][pitch]")
{
    AnalysisPitchNode pitch;
    pitch.prepare ({ sampleRate, block });
    pitch.setParameter ("analysis.pitch.low", 64.0f); // high E string: E4..
    pitch.setParameter ("analysis.pitch.high", 88.0f);
    std::vector<float> signal ((size_t) (0.1 * sampleRate), 0.0f);
    addPluck (signal, 0.0, 329.63);
    const auto out = run (pitch, signal, 3);

    size_t first = signal.size();
    for (size_t i = 0; i < signal.size(); ++i)
        if (out[2][i] > 0.8f && std::abs (out[0][i] - 64.0f) < 0.2f)
        {
            first = i;
            break;
        }
    INFO ("first confident sample " << first << " (" << (double) first / sampleRate * 1000.0 << " ms)");
    CHECK ((double) first / sampleRate < 0.012);
}

TEST_CASE ("Pitch reads zero confidence in silence", "[engine][hearing][pitch]")
{
    AnalysisPitchNode pitch;
    pitch.prepare ({ sampleRate, block });
    const auto out = run (pitch, std::vector<float> ((size_t) (0.1 * sampleRate), 0.0f), 3);
    CHECK (out[2].back() == 0.0f);
}

TEST_CASE ("Onset fires once per pluck, on time, also on a string still ringing", "[engine][hearing][onset]")
{
    AnalysisOnsetNode onset;
    onset.prepare ({ sampleRate, block });
    std::vector<float> signal ((size_t) (1.0 * sampleRate), 0.0f);
    addPluck (signal, 0.1, 329.63);
    addPluck (signal, 0.5, 329.63); // the first one still rings
    const auto out = run (onset, signal, 2);

    std::vector<double> times;
    for (size_t i = 0; i < signal.size(); ++i)
        if (out[0][i] > 0.5f)
            times.push_back ((double) i / sampleRate);
    for (const auto t : times)
        UNSCOPED_INFO ("onset at " << t);
    REQUIRE (times.size() == 2);
    CHECK (times[0] == Catch::Approx (0.1).margin (0.005));
    CHECK (times[1] == Catch::Approx (0.5).margin (0.005));
    CHECK (out[1][(size_t) (0.12 * sampleRate)] > 0.1f);
}

TEST_CASE ("Audio to Notes turns plucks into notes with their pitch, and ends them as they fade", "[engine][hearing][notes]")
{
    AnalysisNotesNode notes;
    notes.prepare ({ sampleRate, block });
    std::vector<float> signal ((size_t) (2.5 * sampleRate), 0.0f);
    addPluck (signal, 0.1, 329.63);  // E4
    addPluck (signal, 0.6, 440.0);   // A4, the E still ringing
    const auto out = runNotes (notes, signal);

    std::vector<std::pair<double, float>> starts;
    std::vector<double> stops;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i].startEvent)
            starts.push_back ({ (double) i / sampleRate, out[i].pitch });
        if (out[i].stopEvent)
            stops.push_back ((double) i / sampleRate);
    }
    REQUIRE (starts.size() == 2);
    CHECK (starts[0].second == Catch::Approx (64.0f).margin (0.2f));
    CHECK (starts[1].second == Catch::Approx (69.0f).margin (0.2f));
    CHECK (starts[0].first < 0.1 + 0.02); // heard within 20 ms
    CHECK (starts[1].first < 0.6 + 0.02);
    REQUIRE (! stops.empty());
    CHECK (stops.back() > 0.6);       // the last note ends …
    CHECK (! out.back().gate);        // … as it fades
    CHECK (out[(size_t) (0.15 * sampleRate)].velocity > 0.0f);
}

TEST_CASE ("Track Phase locks to the played period, and Divide 2 is an octave down", "[engine][hearing][phase]")
{
    for (const auto divide : { 1, 2 })
    {
        AnalysisPhaseNode phase;
        phase.prepare ({ sampleRate, block });
        phase.setParameter ("analysis.phase.divide", (float) divide);
        std::vector<float> signal ((size_t) (0.5 * sampleRate));
        for (size_t i = 0; i < signal.size(); ++i)
            signal[i] = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 200.0 * (double) i / sampleRate);
        const auto out = run (phase, signal, 2);

        INFO ("divide " << divide);
        CHECK (out[1].back() == Catch::Approx (200.0 / divide).epsilon (0.01));

        // A sine read at the tracked phase sounds at 200 / divide Hz.
        std::vector<float> played (signal.size() / 2);
        for (size_t i = 0; i < played.size(); ++i)
            played[i] = (float) std::sin (juce::MathConstants<double>::twoPi * out[0][i + signal.size() / 2]);
        CHECK (magnitudeAt (played, 200.0 / divide) > 0.4);
        if (divide == 2)
            CHECK (magnitudeAt (played, 200.0) < 0.05);
    }
}

TEST_CASE ("An Oscillator driven only by its Phase input is still band-limited", "[engine][hearing][phase]")
{
    // A saw at 2.9 kHz: its 9th harmonic (26.1 kHz) would fold back to 21.9 kHz.
    constexpr double hz = 2900.0;
    const auto unwired = std::numeric_limits<float>::quiet_NaN();
    auto render = [&] (bool byPhase)
    {
        CurveOscillatorNode oscillator;
        oscillator.prepare ({ sampleRate, block });
        REQUIRE (oscillator.setContent (CurveDocument::saw().toVar()));
        std::vector<float> out (9600);
        std::vector<float> in[6];
        for (auto& c : in)
            c.assign (block, 0.0f);
        float result[block];
        double phase = 0.0;
        for (size_t start = 0; start < out.size(); start += block)
        {
            for (int i = 0; i < block; ++i)
            {
                in[0][(size_t) i] = 0.0f;
                in[1][(size_t) i] = byPhase ? 0.0f : (float) hz;
                in[2][(size_t) i] = unwired;
                in[3][(size_t) i] = byPhase ? (float) (phase - std::floor (phase)) : unwired;
                in[4][(size_t) i] = 0.0f;
                in[5][(size_t) i] = unwired;
                phase += hz / sampleRate;
            }
            const float* ins[6] = { in[0].data(), in[1].data(), in[2].data(), in[3].data(), in[4].data(), in[5].data() };
            float* outs[1] = { result };
            oscillator.processBlock (ins, outs, block);
            std::copy (result, result + block, out.begin() + (long) start);
        }
        return out;
    };

    const auto native = render (false);
    const auto driven = render (true);
    INFO ("fundamental " << magnitudeAt (driven, hz) << ", alias at 21.9 kHz: native " << magnitudeAt (native, 21900.0)
                         << ", driven " << magnitudeAt (driven, 21900.0));
    CHECK (magnitudeAt (driven, hz) > 0.25);
    CHECK (magnitudeAt (driven, 21900.0) < 0.01);
}
