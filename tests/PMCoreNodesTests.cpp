// PM Core batch (wiki/NODES.Status.md's own build-next order, step 6) — the
// first real physical-modelling nodes: excite.* produces a signal that
// drives a resonator, resonator.* is the resonating body. This file grows
// batch by batch, same pattern ClockSeqNodesTests.cpp/
// DataFoundationsNodesTests.cpp/NoteStreamNodesTests.cpp already established
// for a multi-node batch landing together.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/nodes/ExciteImpulseNode.h"
#include "bazalt/engine/nodes/ResonatorCombNode.h"
#include "bazalt/engine/nodes/DataMaterialNode.h"
#include "bazalt/engine/nodes/ResonatorModalNode.h"
#include "bazalt/engine/RtAllocationTrap.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

namespace
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

    float runOneSample (Node& node, std::vector<float> inputs)
    {
        float output = 0.0f;
        float* outputs = &output;
        node.processSample (inputs.data(), outputs);
        return output;
    }

    // ResonatorModalNode has 2 real output channels (Stereo) — this variant
    // reads both back, same calling convention runOneSample() above uses for
    // every other (mono) node here.
    std::pair<float, float> runOneStereoSample (Node& node, std::vector<float> inputs)
    {
        float outputs[2] = { 0.0f, 0.0f };
        node.processSample (inputs.data(), outputs);
        return { outputs[0], outputs[1] };
    }

    std::vector<float> dataValues (DataPublisher& publisher)
    {
        const auto* buffer = publisher.getCurrentForAudioThread();
        REQUIRE (buffer != nullptr);
        std::vector<float> values;
        for (int i = 0; i < (int) buffer->rawSize(); ++i)
            values.push_back (buffer->rawData()[i]);
        return values;
    }

    // A synthetic DataBuffer wired directly via setDataInput() — the
    // documented, intended way to unit-test a Data-consuming node without
    // going through a real GraphCompiler/producer (Node.h's own doc comment
    // on setDataInput()/getDataPublisher()).
    struct SyntheticModalSet
    {
        DataPublisher publisher;

        explicit SyntheticModalSet (std::vector<float> interleavedRatioAmpDecay)
        {
            publisher.publish (std::make_unique<DataBuffer> (DataTag::ModalSet, std::move (interleavedRatioAmpDecay), 3));
        }
    };
}

// ---- excite.impulse ----

TEST_CASE ("ExciteImpulseNode is silent until triggered", "[engine][nodes][ExciteImpulseNode][PMCore]")
{
    ExciteImpulseNode node;
    node.prepare ({ 44100.0, 512 });

    for (int i = 0; i < 100; ++i)
        CHECK (runOneSample (node, { 0.0f, kNaN, kNaN }) == 0.0f);
}

TEST_CASE ("ExciteImpulseNode's width == 0 fires exactly one sample at amplitude", "[engine][nodes][ExciteImpulseNode][PMCore]")
{
    ExciteImpulseNode node;
    node.prepare ({ 44100.0, 512 });

    CHECK (runOneSample (node, { 1.0f, 0.8f, 0.0f }) == Catch::Approx (0.8f));
    CHECK (runOneSample (node, { 0.0f, kNaN, kNaN }) == 0.0f);
    CHECK (runOneSample (node, { 0.0f, kNaN, kNaN }) == 0.0f);
}

TEST_CASE ("ExciteImpulseNode's width > 0 widens into a raised-cosine bump of the right length",
           "[engine][nodes][ExciteImpulseNode][PMCore]")
{
    ExciteImpulseNode node;
    node.prepare ({ 1000.0, 512 }); // 1kHz sample rate -> 10ms = 10 samples, easy arithmetic

    std::vector<float> samples;
    samples.push_back (runOneSample (node, { 1.0f, 1.0f, 10.0f }));
    for (int i = 1; i < 12; ++i)
        samples.push_back (runOneSample (node, { 0.0f, kNaN, kNaN }));

    // 10 non-zero samples, then back to silence.
    CHECK (samples[0] == Catch::Approx (0.0f).margin (1e-5)); // Hann window starts at 0
    CHECK (samples[10] == 0.0f); // 11th sample, past the 10-sample window
    CHECK (samples[11] == 0.0f);

    // Peak lands in the middle, roughly at amplitude.
    const auto peak = *std::max_element (samples.begin(), samples.end());
    CHECK (peak > 0.9f);
    CHECK (peak <= 1.0f + 1e-5f);
}

TEST_CASE ("ExciteImpulseNode retriggering restarts cleanly", "[engine][nodes][ExciteImpulseNode][PMCore]")
{
    ExciteImpulseNode node;
    node.prepare ({ 1000.0, 512 });

    runOneSample (node, { 1.0f, 1.0f, 10.0f }); // start a 10-sample bump
    runOneSample (node, { 0.0f, kNaN, kNaN });
    runOneSample (node, { 0.0f, kNaN, kNaN });
    // Retrigger mid-flight with width 0 — should fire a clean single-sample delta next, not
    // continue the old bump's envelope.
    const auto retriggered = runOneSample (node, { 1.0f, 0.5f, 0.0f });
    CHECK (retriggered == Catch::Approx (0.5f));
    CHECK (runOneSample (node, { 0.0f, kNaN, kNaN }) == 0.0f);
}

// ---- resonator.comb ----

TEST_CASE ("ResonatorCombNode is silent on silent input", "[engine][nodes][ResonatorCombNode][PMCore]")
{
    ResonatorCombNode node;
    node.prepare ({ 44100.0, 512 });

    for (int i = 0; i < 1000; ++i)
        CHECK (runOneSample (node, { 0.0f, kNaN, kNaN, kNaN }) == 0.0f);
}

TEST_CASE ("ResonatorCombNode feedforward mode: y[n] = x[n] + g*x[n-M], exact arithmetic",
           "[engine][nodes][ResonatorCombNode][PMCore]")
{
    ResonatorCombNode node;
    node.prepare ({ 1000.0, 512 }); // 1kHz sample rate
    node.setParameter ("resonator.comb.type", 0.0f); // feedforward
    node.setParameter ("resonator.comb.damping", 1.0f); // no smoothing — damped(x) == x exactly

    // frequency = 100Hz -> delaySamples = 1000/100 = 10.
    const float frequency = 100.0f;
    const float feedback = 0.6f;

    std::vector<float> input (30, 0.0f);
    input[0] = 1.0f;
    input[5] = 0.5f;

    std::vector<float> output;
    for (auto x : input)
        output.push_back (runOneSample (node, { x, frequency, feedback, kNaN }));

    // y[0] = x[0] (tap is 0, buffer was empty)
    CHECK (output[0] == Catch::Approx (1.0f));
    // y[10] = x[10] (0) + 0.6 * x[0] (1.0) = 0.6
    CHECK (output[10] == Catch::Approx (0.6f));
    // y[15] = x[15] (0) + 0.6 * x[5] (0.5) = 0.3
    CHECK (output[15] == Catch::Approx (0.3f));
    // Feedforward never re-reads its own output — y[20] should be 0, not
    // influenced by y[10]'s own non-zero value (that's what distinguishes
    // it from feedback mode).
    CHECK (output[20] == Catch::Approx (0.0f).margin (1e-6f));
}

TEST_CASE ("ResonatorCombNode feedback mode rings periodically at the set frequency",
           "[engine][nodes][ResonatorCombNode][PMCore]")
{
    ResonatorCombNode node;
    node.prepare ({ 1000.0, 512 });
    node.setParameter ("resonator.comb.type", 1.0f); // feedback
    node.setParameter ("resonator.comb.damping", 1.0f); // no smoothing, isolate the periodicity

    const float frequency = 100.0f; // delaySamples = 10
    const float feedback = 0.5f;

    std::vector<float> output;
    output.push_back (runOneSample (node, { 1.0f, frequency, feedback, kNaN })); // single impulse
    for (int i = 1; i < 41; ++i)
        output.push_back (runOneSample (node, { 0.0f, frequency, feedback, kNaN }));

    // Energy should reappear every 10 samples, scaled by `feedback` each
    // round trip: output[0]=1, output[10]=0.5, output[20]=0.25, output[30]=0.125.
    CHECK (output[0] == Catch::Approx (1.0f));
    CHECK (output[10] == Catch::Approx (0.5f));
    CHECK (output[20] == Catch::Approx (0.25f));
    CHECK (output[30] == Catch::Approx (0.125f));

    // Samples strictly between the echoes are silent (no smearing without damping).
    CHECK (output[5] == Catch::Approx (0.0f).margin (1e-6f));
    CHECK (output[15] == Catch::Approx (0.0f).margin (1e-6f));
}

TEST_CASE ("ResonatorCombNode's feedback is hard-limited - stays bounded over a long run even if over-driven",
           "[engine][nodes][ResonatorCombNode][PMCore]")
{
    ResonatorCombNode node;
    node.prepare ({ 44100.0, 512 });
    node.setParameter ("resonator.comb.type", 1.0f); // feedback — the mode that could actually diverge

    const auto excited = runOneSample (node, { 1.0f, 440.0f, 5.0f, 0.5f }); // feedback way over 1
    juce::ignoreUnused (excited);

    for (int i = 0; i < 44100 * 2; ++i)
    {
        const auto y = runOneSample (node, { 0.0f, 440.0f, 5.0f, 0.5f });
        REQUIRE (std::isfinite (y));
        REQUIRE (std::fabs (y) < 1000.0f); // nowhere near divergence; a real runaway would be enormous
    }
}

TEST_CASE ("ResonatorCombNode's damping follows filter.onepole's own convention: 0 = darkest, 1 = brightest",
           "[engine][nodes][ResonatorCombNode][PMCore]")
{
    ResonatorCombNode brightNode, darkNode;
    brightNode.prepare ({ 1000.0, 512 });
    darkNode.prepare ({ 1000.0, 512 });
    brightNode.setParameter ("resonator.comb.type", 1.0f);
    darkNode.setParameter ("resonator.comb.type", 1.0f);
    brightNode.setParameter ("resonator.comb.damping", 1.0f);
    darkNode.setParameter ("resonator.comb.damping", 0.0f);

    const float frequency = 100.0f;
    const float feedback = 0.9f;

    runOneSample (brightNode, { 1.0f, frequency, feedback, kNaN });
    runOneSample (darkNode, { 1.0f, frequency, feedback, kNaN });

    for (int i = 0; i < 9; ++i)
    {
        runOneSample (brightNode, { 0.0f, frequency, feedback, kNaN });
        runOneSample (darkNode, { 0.0f, frequency, feedback, kNaN });
    }

    // At the first echo (sample 10): bright (damping=1, no smoothing) passes
    // the full impulse through at feedback*1 = 0.9. Dark (damping=0, the
    // in-loop filter state never moves off its initial 0) delivers nothing.
    const auto brightEcho = runOneSample (brightNode, { 0.0f, frequency, feedback, kNaN });
    const auto darkEcho = runOneSample (darkNode, { 0.0f, frequency, feedback, kNaN });

    CHECK (brightEcho == Catch::Approx (0.9f));
    CHECK (darkEcho == Catch::Approx (0.0f).margin (1e-6f));
}

// ---- data.material ----

TEST_CASE ("DataMaterialNode publishes a modal-set buffer with the right shape",
           "[engine][nodes][DataMaterialNode][PMCore]")
{
    DataMaterialNode node;
    node.prepare ({ 44100.0, 512 });

    const auto* buffer = node.getDataPublisher()->getCurrentForAudioThread();
    REQUIRE (buffer != nullptr);
    CHECK (buffer->tag() == DataTag::ModalSet);
    CHECK (buffer->stride() == 3);
    CHECK (buffer->length() == 32); // default modeCount
}

TEST_CASE ("DataMaterialNode's string geometry is the plain harmonic series with no inharmonicity",
           "[engine][nodes][DataMaterialNode][PMCore]")
{
    DataMaterialNode node;
    node.prepare ({ 44100.0, 512 });
    node.setParameter ("data.material.geometry", 0.0f); // string
    node.setParameter ("data.material.inharmonicity", 0.0f);
    node.setParameter ("data.material.modeCount", 5.0f);

    const auto* buffer = node.getDataPublisher()->getCurrentForAudioThread();
    REQUIRE (buffer != nullptr);
    REQUIRE (buffer->length() == 5);
    for (int i = 0; i < 5; ++i)
        CHECK (buffer->at (i, 0) == Catch::Approx ((float) (i + 1)));
}

TEST_CASE ("DataMaterialNode's tube geometry is odd harmonics only",
           "[engine][nodes][DataMaterialNode][PMCore]")
{
    DataMaterialNode node;
    node.prepare ({ 44100.0, 512 });
    node.setParameter ("data.material.geometry", 2.0f); // tube
    node.setParameter ("data.material.inharmonicity", 0.0f);
    node.setParameter ("data.material.modeCount", 4.0f);

    const auto* buffer = node.getDataPublisher()->getCurrentForAudioThread();
    REQUIRE (buffer != nullptr);
    CHECK (buffer->at (0, 0) == Catch::Approx (1.0f));
    CHECK (buffer->at (1, 0) == Catch::Approx (3.0f));
    CHECK (buffer->at (2, 0) == Catch::Approx (5.0f));
    CHECK (buffer->at (3, 0) == Catch::Approx (7.0f));
}

TEST_CASE ("DataMaterialNode's bar geometry matches the known free-free-beam reference ratios within 2%",
           "[engine][nodes][DataMaterialNode][PMCore]")
{
    DataMaterialNode node;
    node.prepare ({ 44100.0, 512 });
    node.setParameter ("data.material.geometry", 1.0f); // bar
    node.setParameter ("data.material.inharmonicity", 0.0f);
    node.setParameter ("data.material.modeCount", 4.0f);

    const auto* buffer = node.getDataPublisher()->getCurrentForAudioThread();
    REQUIRE (buffer != nullptr);
    // Standard reference values (Fletcher & Rossing) for a free-free bar's
    // flexural modes: 1, 2.756, 5.404, 8.933.
    CHECK (buffer->at (0, 0) == Catch::Approx (1.0f).epsilon (0.02));
    CHECK (buffer->at (1, 0) == Catch::Approx (2.756f).epsilon (0.02));
    CHECK (buffer->at (2, 0) == Catch::Approx (5.404f).epsilon (0.02));
    CHECK (buffer->at (3, 0) == Catch::Approx (8.933f).epsilon (0.02));
}

TEST_CASE ("DataMaterialNode's inharmonicity+stiffness stretch higher modes sharp, monotonically",
           "[engine][nodes][DataMaterialNode][PMCore]")
{
    DataMaterialNode flat, stretched;
    flat.prepare ({ 44100.0, 512 });
    stretched.prepare ({ 44100.0, 512 });
    flat.setParameter ("data.material.inharmonicity", 0.0f);
    stretched.setParameter ("data.material.inharmonicity", 1.0f);
    stretched.setParameter ("data.material.stiffness", 1.0f);

    const auto flatMode32 = flat.getDataPublisher()->getCurrentForAudioThread()->at (31, 0);
    const auto stretchedMode32 = stretched.getDataPublisher()->getCurrentForAudioThread()->at (31, 0);

    CHECK (flatMode32 == Catch::Approx (32.0f)); // pure harmonic, mode 32 of a string
    CHECK (stretchedMode32 > flatMode32); // real inharmonicity always stretches SHARP, never flat
}

TEST_CASE ("DataMaterialNode's irregularity is deterministic for the same seed, across separate instances",
           "[engine][nodes][DataMaterialNode][PMCore]")
{
    DataMaterialNode a, b;
    a.prepare ({ 44100.0, 512 });
    b.prepare ({ 44100.0, 512 });
    a.setParameter ("data.material.irregularity", 0.8f);
    a.setParameter ("data.material.seed", 42.0f);
    b.setParameter ("data.material.irregularity", 0.8f);
    b.setParameter ("data.material.seed", 42.0f);

    CHECK (dataValues (*a.getDataPublisher()) == dataValues (*b.getDataPublisher()));

    b.setParameter ("data.material.seed", 43.0f);
    CHECK (dataValues (*a.getDataPublisher()) != dataValues (*b.getDataPublisher()));
}

TEST_CASE ("DataMaterialNode's decay weight is flat at 1.0 unless BOTH density and damping are non-zero",
           "[engine][nodes][DataMaterialNode][PMCore]")
{
    DataMaterialNode node;
    node.prepare ({ 44100.0, 512 });
    node.setParameter ("data.material.modeCount", 8.0f);
    node.setParameter ("data.material.density", 0.0f);
    node.setParameter ("data.material.damping", 1.0f); // damping alone, density == 0 -> no effect

    const auto* flatBuffer = node.getDataPublisher()->getCurrentForAudioThread();
    for (int i = 0; i < 8; ++i)
        CHECK (flatBuffer->at (i, 2) == Catch::Approx (1.0f));

    node.setParameter ("data.material.density", 1.0f); // now both non-zero
    const auto* decayingBuffer = node.getDataPublisher()->getCurrentForAudioThread();
    CHECK (decayingBuffer->at (0, 2) == Catch::Approx (1.0f)); // fundamental is always 1.0
    CHECK (decayingBuffer->at (7, 2) < decayingBuffer->at (3, 2)); // higher modes decay faster (lower weight)
    CHECK (decayingBuffer->at (3, 2) < decayingBuffer->at (1, 2));
}

TEST_CASE ("DataMaterialNode's preset shapes amplitude rolloff - metal stays louder than wood at high modes",
           "[engine][nodes][DataMaterialNode][PMCore]")
{
    DataMaterialNode wood, metal;
    wood.prepare ({ 44100.0, 512 });
    metal.prepare ({ 44100.0, 512 });
    wood.setParameter ("data.material.modeCount", 16.0f);
    metal.setParameter ("data.material.modeCount", 16.0f);
    wood.setParameter ("data.material.preset", 0.0f);  // wood
    metal.setParameter ("data.material.preset", 2.0f); // metal

    const auto woodMode16 = wood.getDataPublisher()->getCurrentForAudioThread()->at (15, 1);
    const auto metalMode16 = metal.getDataPublisher()->getCurrentForAudioThread()->at (15, 1);

    CHECK (metalMode16 > woodMode16);
    // Both presets agree exactly at the fundamental (n^-exponent == 1 at n=1 regardless of exponent).
    CHECK (wood.getDataPublisher()->getCurrentForAudioThread()->at (0, 1) == Catch::Approx (1.0f));
    CHECK (metal.getDataPublisher()->getCurrentForAudioThread()->at (0, 1) == Catch::Approx (1.0f));
}

TEST_CASE ("DataMaterialNode republishes (a new generation) on every relevant setParameter call",
           "[engine][nodes][DataMaterialNode][PMCore]")
{
    DataMaterialNode node;
    node.prepare ({ 44100.0, 512 });
    const auto firstGeneration = node.getDataPublisher()->getCurrentForAudioThread()->generation;

    node.setParameter ("data.material.stiffness", 0.9f);
    const auto secondGeneration = node.getDataPublisher()->getCurrentForAudioThread()->generation;

    CHECK (secondGeneration > firstGeneration);
}

TEST_CASE ("DataMaterialNode's read side (getCurrentForAudioThread) is allocation-free",
           "[engine][nodes][DataMaterialNode][PMCore]")
{
    DataMaterialNode node;
    node.prepare ({ 44100.0, 512 });

    const DataBuffer* buffer = nullptr;
    {
        ScopedAudioThreadAllocationTrap trap;
        buffer = node.getDataPublisher()->getCurrentForAudioThread();
    }
    CHECK (buffer != nullptr);
}

// ---- resonator.modal ----

TEST_CASE ("ResonatorModalNode is silent with no modes connected", "[engine][nodes][ResonatorModalNode][PMCore]")
{
    ResonatorModalNode node;
    node.prepare ({ 44100.0, 512 });

    for (int i = 0; i < 100; ++i)
    {
        const auto [l, r] = runOneStereoSample (node, { 1.0f, 0.0f, kNaN, kNaN, kNaN, kNaN, kNaN, kNaN });
        CHECK (l == 0.0f);
        CHECK (r == 0.0f);
    }
}

TEST_CASE ("ResonatorModalNode is silent if the wired Data buffer isn't a ModalSet",
           "[engine][nodes][ResonatorModalNode][PMCore]")
{
    ResonatorModalNode node;
    node.prepare ({ 44100.0, 512 });

    DataPublisher wrongTag;
    wrongTag.publish (std::make_unique<DataBuffer> (DataTag::Curve, std::vector<float> { 1.0f, 1.0f, 1.0f }, 3));
    node.setDataInput ("modes", &wrongTag);

    const auto [l, r] = runOneStereoSample (node, { 1.0f, 0.0f, kNaN, kNaN, kNaN, kNaN, kNaN, kNaN });
    CHECK (l == 0.0f);
    CHECK (r == 0.0f);
}

TEST_CASE ("ResonatorModalNode rings at the expected frequency for a single unison mode",
           "[engine][nodes][ResonatorModalNode][PMCore]")
{
    ResonatorModalNode node;
    node.prepare ({ 44100.0, 512 });
    SyntheticModalSet oneMode ({ 1.0f, 1.0f, 1.0f }); // ratio=1, ampWeight=1, decayWeight=1
    node.setDataInput ("modes", &oneMode.publisher);
    node.setParameter ("resonator.modal.position", 0.5f); // avoid nulling the only mode
    node.setParameter ("resonator.modal.spread", 0.0f);   // keep it mono/centred for a simple zero-crossing count

    const float pitch = 69.0f; // A4, 440Hz exactly (440 * 2^0 == 440)
    runOneStereoSample (node, { 1.0f, 0.0f, pitch, 2.0f, 1.0f, 0.0f, 0.5f, 0.0f }); // single-sample excitation

    int zeroCrossings = 0;
    float previous = 0.0f;
    const int window = 2205; // 50ms @ 44100Hz -> ~22 cycles @ 440Hz expected -> ~44 zero crossings
    for (int i = 0; i < window; ++i)
    {
        const auto [l, r] = runOneStereoSample (node, { 0.0f, 0.0f, pitch, 2.0f, 1.0f, 0.0f, 0.5f, 0.0f });
        juce::ignoreUnused (r);
        if ((previous < 0.0f && l >= 0.0f) || (previous > 0.0f && l <= 0.0f))
            ++zeroCrossings;
        previous = l;
    }

    // ~44 expected; generous tolerance since the resonator isn't an ideal oscillator.
    CHECK (zeroCrossings > 30);
    CHECK (zeroCrossings < 58);
}

TEST_CASE ("ResonatorModalNode's position == 0 silences the output entirely (a real physical null, not a bug)",
           "[engine][nodes][ResonatorModalNode][PMCore]")
{
    ResonatorModalNode node;
    node.prepare ({ 44100.0, 512 });
    SyntheticModalSet modes ({ 1.0f, 1.0f, 1.0f, 2.0f, 1.0f, 1.0f, 3.0f, 1.0f, 1.0f });
    node.setDataInput ("modes", &modes.publisher);
    node.setParameter ("resonator.modal.position", 0.0f);

    for (int i = 0; i < 200; ++i)
    {
        const auto inputs = i == 0 ? std::vector<float> { 1.0f, 0.0f, 69.0f, 2.0f, 1.0f, 0.0f, 0.0f, 0.0f }
                                    : std::vector<float> { 0.0f, 0.0f, 69.0f, 2.0f, 1.0f, 0.0f, 0.0f, 0.0f };
        const auto [l, r] = runOneStereoSample (node, inputs);
        CHECK (l == 0.0f);
        CHECK (r == 0.0f);
    }
}

TEST_CASE ("ResonatorModalNode's decay knob orders sustained energy: longer decay keeps more energy after the same time",
           "[engine][nodes][ResonatorModalNode][PMCore]")
{
    ResonatorModalNode shortDecay, longDecay;
    shortDecay.prepare ({ 44100.0, 512 });
    longDecay.prepare ({ 44100.0, 512 });
    SyntheticModalSet modesA ({ 1.0f, 1.0f, 1.0f });
    SyntheticModalSet modesB ({ 1.0f, 1.0f, 1.0f });
    shortDecay.setDataInput ("modes", &modesA.publisher);
    longDecay.setDataInput ("modes", &modesB.publisher);

    const auto excite = [] (Node& n, float decay)
    {
        runOneStereoSample (n, { 1.0f, 0.0f, 69.0f, decay, 1.0f, 0.0f, 0.5f, 0.0f });
    };
    excite (shortDecay, 0.05f);
    excite (longDecay, 3.0f);

    float rmsShort = 0.0f, rmsLong = 0.0f;
    const int window = 4410; // 100ms trailing window, taken after letting both ring for a while
    for (int i = 0; i < 44100; ++i)
    {
        const auto [ls, rs] = runOneStereoSample (shortDecay, { 0.0f, 0.0f, 69.0f, 0.05f, 1.0f, 0.0f, 0.5f, 0.0f });
        const auto [ll, rl] = runOneStereoSample (longDecay, { 0.0f, 0.0f, 69.0f, 3.0f, 1.0f, 0.0f, 0.5f, 0.0f });
        juce::ignoreUnused (rs, rl);
        if (i >= 44100 - window)
        {
            rmsShort += ls * ls;
            rmsLong += ll * ll;
        }
    }

    CHECK (rmsLong > rmsShort);
}

TEST_CASE ("ResonatorModalNode's maxModes structurally caps how many modes of the Data buffer are used",
           "[engine][nodes][ResonatorModalNode][PMCore]")
{
    ResonatorModalNode capped, uncapped;
    capped.setParameter ("resonator.modal.maxModes", 1.0f);
    capped.prepare ({ 44100.0, 512 });
    uncapped.prepare ({ 44100.0, 512 }); // default maxModes (64)

    // Same first mode in both; the capped node's buffer ALSO has 2 more modes it must ignore.
    SyntheticModalSet threeModes ({ 1.0f, 1.0f, 1.0f, 2.0f, 1.0f, 1.0f, 3.0f, 1.0f, 1.0f });
    SyntheticModalSet oneMode ({ 1.0f, 1.0f, 1.0f });
    capped.setDataInput ("modes", &threeModes.publisher);
    uncapped.setDataInput ("modes", &oneMode.publisher);

    for (int i = 0; i < 500; ++i)
    {
        const auto inputs = i == 0 ? std::vector<float> { 1.0f, 0.0f, 69.0f, 2.0f, 1.0f, 0.0f, 0.5f, 0.0f }
                                    : std::vector<float> { 0.0f, 0.0f, 69.0f, 2.0f, 1.0f, 0.0f, 0.5f, 0.0f };
        const auto cappedOut = runOneStereoSample (capped, inputs);
        const auto uncappedOut = runOneStereoSample (uncapped, inputs);
        CHECK (cappedOut.first == Catch::Approx (uncappedOut.first).margin (1e-5f));
        CHECK (cappedOut.second == Catch::Approx (uncappedOut.second).margin (1e-5f));
    }
}

TEST_CASE ("ResonatorModalNode's spread pans multi-mode output: 0 is mono-centred, >0 splits left/right",
           "[engine][nodes][ResonatorModalNode][PMCore]")
{
    ResonatorModalNode mono, spread;
    mono.prepare ({ 44100.0, 512 });
    spread.prepare ({ 44100.0, 512 });
    SyntheticModalSet modesA ({ 1.0f, 1.0f, 1.0f, 2.3f, 1.0f, 1.0f, 3.7f, 1.0f, 1.0f });
    SyntheticModalSet modesB ({ 1.0f, 1.0f, 1.0f, 2.3f, 1.0f, 1.0f, 3.7f, 1.0f, 1.0f });
    mono.setDataInput ("modes", &modesA.publisher);
    spread.setDataInput ("modes", &modesB.publisher);
    mono.setParameter ("resonator.modal.spread", 0.0f);
    spread.setParameter ("resonator.modal.spread", 1.0f);

    bool everDiffered = false;
    for (int i = 0; i < 500; ++i)
    {
        const auto inputs = i == 0 ? std::vector<float> { 1.0f, 0.0f, 69.0f, 2.0f, 1.0f, 0.0f, 0.5f, kNaN }
                                    : std::vector<float> { 0.0f, 0.0f, 69.0f, 2.0f, 1.0f, 0.0f, 0.5f, kNaN };

        auto monoInputs = inputs; monoInputs[7] = 0.0f;
        auto spreadInputs = inputs; spreadInputs[7] = 1.0f;

        const auto [ml, mr] = runOneStereoSample (mono, monoInputs);
        CHECK (ml == Catch::Approx (mr)); // spread == 0 is always exactly mono

        const auto [sl, sr] = runOneStereoSample (spread, spreadInputs);
        if (std::fabs (sl - sr) > 1e-4f)
            everDiffered = true;
    }

    CHECK (everDiffered);
}

TEST_CASE ("ResonatorModalNode stays finite and bounded over a long run at full maxModes",
           "[engine][nodes][ResonatorModalNode][PMCore]")
{
    ResonatorModalNode node;
    node.prepare ({ 44100.0, 512 });

    std::vector<float> sixtyFourModes;
    for (int i = 0; i < 64; ++i)
    {
        sixtyFourModes.push_back ((float) (i + 1));
        sixtyFourModes.push_back (1.0f / (float) (i + 1));
        sixtyFourModes.push_back (1.0f);
    }
    SyntheticModalSet modes (sixtyFourModes);
    node.setDataInput ("modes", &modes.publisher);
    node.setParameter ("resonator.modal.inharmonicity", 1.0f);
    node.setParameter ("resonator.modal.brightness", 0.2f);

    for (int i = 0; i < 44100 * 2; ++i)
    {
        const auto inputs = i < 10
            ? std::vector<float> { 1.0f, 0.0f, 100.0f, 2.0f, 0.2f, 1.0f, 0.5f, 0.7f }
            : std::vector<float> { 0.0f, 0.0f, 100.0f, 2.0f, 0.2f, 1.0f, 0.5f, 0.7f };
        const auto [l, r] = runOneStereoSample (node, inputs);
        REQUIRE (std::isfinite (l));
        REQUIRE (std::isfinite (r));
        REQUIRE (std::fabs (l) < 1000.0f);
        REQUIRE (std::fabs (r) < 1000.0f);
    }
}
