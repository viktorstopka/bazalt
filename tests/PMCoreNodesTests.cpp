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
#include "bazalt/engine/nodes/ExcitePluckNode.h"
#include "bazalt/engine/nodes/ResonatorStringNode.h"
#include "bazalt/engine/nodes/ExciteMalletNode.h"
#include "bazalt/engine/nodes/ResonatorPlateNode.h"
#include "bazalt/engine/graph/GraphCompiler.h"
#include "bazalt/engine/graph/ProofGraphs.h"
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

// ---- excite.pluck ----

TEST_CASE ("ExcitePluckNode is silent until triggered", "[engine][nodes][ExcitePluckNode][PMCore]")
{
    ExcitePluckNode node;
    node.prepare ({ 44100.0, 512 });

    for (int i = 0; i < 100; ++i)
        CHECK (runOneSample (node, { 0.0f, kNaN, kNaN, kNaN }) == 0.0f);
}

TEST_CASE ("ExcitePluckNode's amplitude == 0 is exact silence regardless of the noise generator",
           "[engine][nodes][ExcitePluckNode][PMCore]")
{
    ExcitePluckNode node;
    node.prepare ({ 44100.0, 512 });

    CHECK (runOneSample (node, { 1.0f, 0.3f, 0.5f, 0.0f }) == 0.0f);
    for (int i = 0; i < 300; ++i)
        CHECK (runOneSample (node, { 0.0f, kNaN, kNaN, kNaN }) == 0.0f);
}

TEST_CASE ("ExcitePluckNode's burst lasts exactly burstDurationMs, then goes silent",
           "[engine][nodes][ExcitePluckNode][PMCore]")
{
    ExcitePluckNode node;
    node.prepare ({ 1000.0, 512 }); // 1kHz -> 5ms == 5 samples exactly

    runOneSample (node, { 1.0f, 0.3f, 1.0f, 1.0f });
    for (int i = 1; i < 5; ++i)
        runOneSample (node, { 0.0f, kNaN, kNaN, kNaN });

    // 6th sample onward: silent.
    CHECK (runOneSample (node, { 0.0f, kNaN, kNaN, kNaN }) == 0.0f);
    CHECK (runOneSample (node, { 0.0f, kNaN, kNaN, kNaN }) == 0.0f);
}

TEST_CASE ("ExcitePluckNode samples amplitude/hardness/position once at trigger, not live during decay",
           "[engine][nodes][ExcitePluckNode][PMCore]")
{
    ExcitePluckNode node;
    node.prepare ({ 1000.0, 512 });

    runOneSample (node, { 1.0f, 0.3f, 1.0f, 1.0f }); // fires with amplitude 1.0

    // Changing amplitude to 0 via setParameter mid-decay must NOT retroactively
    // silence this already-fired pluck (it was captured at trigger time).
    node.setParameter ("excite.pluck.amplitude", 0.0f);

    bool anyNonZero = false;
    for (int i = 1; i < 5; ++i)
        if (runOneSample (node, { 0.0f, kNaN, kNaN, kNaN }) != 0.0f)
            anyNonZero = true;

    CHECK (anyNonZero);
}

// ---- resonator.string ----

TEST_CASE ("ResonatorStringNode is silent with no excitation", "[engine][nodes][ResonatorStringNode][PMCore]")
{
    ResonatorStringNode node;
    node.prepare ({ 44100.0, 512 });

    float outputs[2];
    const float inputs[7] = { 0.0f, 69.0f, 3.0f, 0.5f, 0.1f, 0.15f, 1.0f };
    for (int i = 0; i < 1000; ++i)
    {
        node.processSample (inputs, outputs);
        CHECK (outputs[0] == 0.0f);
        CHECK (outputs[1] == 0.0f);
    }
}

TEST_CASE ("ResonatorStringNode rings at the expected pitch after a single-sample excitation",
           "[engine][nodes][ResonatorStringNode][PMCore]")
{
    ResonatorStringNode node;
    node.prepare ({ 44100.0, 512 });

    float outputs[2];
    const float excited[7] = { 1.0f, 69.0f, 3.0f, 1.0f, 0.0f, 0.0f, 1.0f }; // damping=1 (brightest), stiffness=0 (no dispersion)
    node.processSample (excited, outputs);

    const float quiet[7] = { 0.0f, 69.0f, 3.0f, 1.0f, 0.0f, 0.0f, 1.0f };
    int zeroCrossings = 0;
    float previous = outputs[0];
    const int window = 2205; // 50ms @ 44100Hz -> ~22 cycles @ 440Hz -> ~44 zero crossings
    for (int i = 0; i < window; ++i)
    {
        node.processSample (quiet, outputs);
        if ((previous < 0.0f && outputs[0] >= 0.0f) || (previous > 0.0f && outputs[0] <= 0.0f))
            ++zeroCrossings;
        previous = outputs[0];
    }

    CHECK (zeroCrossings > 30);
    CHECK (zeroCrossings < 58);
}

TEST_CASE ("ResonatorStringNode's decay orders sustained energy: longer decay keeps more energy after the same time",
           "[engine][nodes][ResonatorStringNode][PMCore]")
{
    ResonatorStringNode shortDecay, longDecay;
    shortDecay.prepare ({ 44100.0, 512 });
    longDecay.prepare ({ 44100.0, 512 });

    float outputs[2];
    const float shortExcited[7] = { 1.0f, 69.0f, 0.1f, 0.5f, 0.1f, 0.15f, 1.0f };
    const float longExcited[7] = { 1.0f, 69.0f, 5.0f, 0.5f, 0.1f, 0.15f, 1.0f };
    shortDecay.processSample (shortExcited, outputs);
    longDecay.processSample (longExcited, outputs);

    const float shortQuiet[7] = { 0.0f, 69.0f, 0.1f, 0.5f, 0.1f, 0.15f, 1.0f };
    const float longQuiet[7] = { 0.0f, 69.0f, 5.0f, 0.5f, 0.1f, 0.15f, 1.0f };

    float rmsShort = 0.0f, rmsLong = 0.0f;
    const int total = 44100, window = 4410;
    for (int i = 0; i < total; ++i)
    {
        shortDecay.processSample (shortQuiet, outputs);
        const auto ls = outputs[0];
        longDecay.processSample (longQuiet, outputs);
        const auto ll = outputs[0];

        if (i >= total - window)
        {
            rmsShort += ls * ls;
            rmsLong += ll * ll;
        }
    }

    CHECK (rmsLong > rmsShort);
}

TEST_CASE ("ResonatorStringNode's release gate mutes the string faster than holding it",
           "[engine][nodes][ResonatorStringNode][PMCore]")
{
    ResonatorStringNode held, released;
    held.prepare ({ 44100.0, 512 });
    released.prepare ({ 44100.0, 512 });

    float outputs[2];
    const float excited[7] = { 1.0f, 69.0f, 10.0f, 0.3f, 0.1f, 0.15f, 1.0f }; // long decay so release's own effect dominates
    held.processSample (excited, outputs);
    released.processSample (excited, outputs);

    const float heldQuiet[7] = { 0.0f, 69.0f, 10.0f, 0.3f, 0.1f, 0.15f, 1.0f };   // release == true (held)
    const float releasedQuiet[7] = { 0.0f, 69.0f, 10.0f, 0.3f, 0.1f, 0.15f, 0.0f }; // release == false

    float rmsHeld = 0.0f, rmsReleased = 0.0f;
    const int total = 8820; // 200ms, long enough for the ~15ms release ramp to fully take effect
    const int window = 441; // last 10ms
    for (int i = 0; i < total; ++i)
    {
        held.processSample (heldQuiet, outputs);
        const auto h = outputs[0];
        released.processSample (releasedQuiet, outputs);
        const auto r = outputs[0];

        if (i >= total - window)
        {
            rmsHeld += h * h;
            rmsReleased += r * r;
        }
    }

    CHECK (rmsReleased < rmsHeld);
}

TEST_CASE ("ResonatorStringNode's out and motion are real, distinct signals when position > 0",
           "[engine][nodes][ResonatorStringNode][PMCore]")
{
    ResonatorStringNode node;
    node.prepare ({ 44100.0, 512 });

    float outputs[2];
    const float excited[7] = { 1.0f, 69.0f, 3.0f, 0.3f, 0.1f, 0.3f, 1.0f };
    node.processSample (excited, outputs);

    const float quiet[7] = { 0.0f, 69.0f, 3.0f, 0.3f, 0.1f, 0.3f, 1.0f };
    bool everDiffered = false;
    bool everNonZero = false;
    for (int i = 0; i < 500; ++i)
    {
        node.processSample (quiet, outputs);
        if (std::fabs (outputs[0] - outputs[1]) > 1e-6f)
            everDiffered = true;
        if (outputs[1] != 0.0f)
            everNonZero = true;
    }

    CHECK (everDiffered);
    CHECK (everNonZero);
}

TEST_CASE ("ResonatorStringNode stays finite and bounded over a long run",
           "[engine][nodes][ResonatorStringNode][PMCore]")
{
    ResonatorStringNode node;
    node.prepare ({ 44100.0, 512 });

    float outputs[2];
    for (int i = 0; i < 44100 * 2; ++i)
    {
        const float inputs[7] = { i < 10 ? 0.8f : 0.0f, 48.0f, 8.0f, 0.2f, 0.9f, 0.4f, 1.0f };
        node.processSample (inputs, outputs);
        REQUIRE (std::isfinite (outputs[0]));
        REQUIRE (std::isfinite (outputs[1]));
        REQUIRE (std::fabs (outputs[0]) < 1000.0f);
        REQUIRE (std::fabs (outputs[1]) < 1000.0f);
    }
}

// ---- excite.mallet ----

TEST_CASE ("ExciteMalletNode is silent and has no contact until triggered", "[engine][nodes][ExciteMalletNode][PMCore]")
{
    ExciteMalletNode node;
    node.prepare ({ 44100.0, 512 });

    float outputs[2];
    const float quiet[5] = { 0.0f, kNaN, kNaN, kNaN, 0.0f };
    for (int i = 0; i < 100; ++i)
    {
        node.processSample (quiet, outputs);
        CHECK (outputs[0] == 0.0f);
        CHECK (outputs[1] == 0.0f);
    }
}

TEST_CASE ("ExciteMalletNode's contact is a one-shot half-sine pulse: true during, false and silent after",
           "[engine][nodes][ExciteMalletNode][PMCore]")
{
    ExciteMalletNode node;
    node.prepare ({ 44100.0, 512 });

    float outputs[2];
    const float trigger[5] = { 1.0f, 0.8f, 0.3f, 0.5f, 0.0f };
    node.processSample (trigger, outputs);
    CHECK (outputs[1] == 1.0f); // contact == true

    const float quiet[5] = { 0.0f, kNaN, kNaN, kNaN, 0.0f };
    int contactSamples = 1;
    while (contactSamples < 10000)
    {
        node.processSample (quiet, outputs);
        if (outputs[1] == 0.0f)
            break;
        ++contactSamples;
    }

    REQUIRE (contactSamples < 10000); // contact must end on its own - a real one-shot, not a forever-ringing node
    CHECK (outputs[0] == 0.0f); // silent the instant contact ends

    // Stays silent afterward.
    for (int i = 0; i < 50; ++i)
    {
        node.processSample (quiet, outputs);
        CHECK (outputs[0] == 0.0f);
        CHECK (outputs[1] == 0.0f);
    }
}

TEST_CASE ("ExciteMalletNode's stiffness shortens contact duration; mass lengthens it",
           "[engine][nodes][ExciteMalletNode][PMCore]")
{
    auto contactDuration = [] (float stiffness, float mass)
    {
        ExciteMalletNode node;
        node.prepare ({ 44100.0, 512 });
        float outputs[2];
        const float trigger[5] = { 1.0f, 0.8f, mass, stiffness, 0.0f };
        node.processSample (trigger, outputs);
        const float quiet[5] = { 0.0f, kNaN, kNaN, kNaN, 0.0f };
        int samples = 1;
        while (samples < 10000)
        {
            node.processSample (quiet, outputs);
            if (outputs[1] == 0.0f)
                break;
            ++samples;
        }
        return samples;
    };

    const auto softStiff = contactDuration (0.1f, 0.3f);
    const auto hardStiff = contactDuration (0.9f, 0.3f);
    CHECK (hardStiff < softStiff); // stiffer -> shorter contact

    const auto lightMass = contactDuration (0.5f, 0.1f);
    const auto heavyMass = contactDuration (0.5f, 0.9f);
    CHECK (heavyMass > lightMass); // heavier -> longer contact
}

TEST_CASE ("ExciteMalletNode's feedback measurably reduces the effective driving velocity",
           "[engine][nodes][ExciteMalletNode][PMCore]")
{
    ExciteMalletNode uncoupled, opposed;
    uncoupled.prepare ({ 44100.0, 512 });
    opposed.prepare ({ 44100.0, 512 });

    float outputs[2];
    const float trigger[5] = { 1.0f, 1.0f, 0.3f, 0.5f, 0.0f };
    uncoupled.processSample (trigger, outputs);
    opposed.processSample (trigger, outputs);

    const float noFeedback[5] = { 0.0f, kNaN, kNaN, kNaN, 0.0f };
    const float withFeedback[5] = { 0.0f, kNaN, kNaN, kNaN, 1.0f }; // a resonator pushing back hard

    uncoupled.processSample (noFeedback, outputs);
    const auto uncoupledOut = outputs[0];
    opposed.processSample (withFeedback, outputs);
    const auto opposedOut = outputs[0];

    CHECK (opposedOut < uncoupledOut); // real coupling: positive feedback measurably reduces output
}

TEST_CASE ("ExciteMalletNode retriggering mid-contact restarts cleanly", "[engine][nodes][ExciteMalletNode][PMCore]")
{
    ExciteMalletNode node;
    node.prepare ({ 44100.0, 512 });

    float outputs[2];
    const float trigger1[5] = { 1.0f, 0.3f, 0.3f, 0.5f, 0.0f };
    node.processSample (trigger1, outputs);
    node.processSample (trigger1, outputs); // let it ring a tiny bit

    const float trigger2[5] = { 1.0f, 1.0f, 0.3f, 0.5f, 0.0f }; // louder retrigger
    node.processSample (trigger2, outputs);
    CHECK (outputs[1] == 1.0f); // contact restarted
}

// ---- resonator.plate ----

TEST_CASE ("ResonatorPlateNode is silent on silent input", "[engine][nodes][ResonatorPlateNode][PMCore]")
{
    ResonatorPlateNode node;
    node.prepare ({ 44100.0, 512 });

    float outputs[2];
    const float quiet[7] = { 0.0f, 0.5f, 0.5f, 2.0f, 1.0f, 0.4f, 0.6f };
    for (int i = 0; i < 500; ++i)
    {
        node.processSample (quiet, outputs);
        CHECK (outputs[0] == 0.0f);
        CHECK (outputs[1] == 0.0f);
    }
}

TEST_CASE ("ResonatorPlateNode's size and tension both raise the fundamental (smaller/tenser -> higher)",
           "[engine][nodes][ResonatorPlateNode][PMCore]")
{
    auto zeroCrossingsFor = [] (float size, float tension)
    {
        ResonatorPlateNode node;
        node.prepare ({ 44100.0, 512 });
        float outputs[2];
        const float excited[7] = { 1.0f, size, tension, 3.0f, 1.0f, 0.4f, 0.6f };
        node.processSample (excited, outputs);
        const float quiet[7] = { 0.0f, size, tension, 3.0f, 1.0f, 0.4f, 0.6f };
        int crossings = 0;
        float previous = outputs[0];
        for (int i = 0; i < 2205; ++i)
        {
            node.processSample (quiet, outputs);
            if ((previous < 0.0f && outputs[0] >= 0.0f) || (previous > 0.0f && outputs[0] <= 0.0f))
                ++crossings;
            previous = outputs[0];
        }
        return crossings;
    };

    CHECK (zeroCrossingsFor (0.0f, 0.5f) > zeroCrossingsFor (1.0f, 0.5f)); // smaller (size=0) rings higher than larger (size=1)
    CHECK (zeroCrossingsFor (0.5f, 1.0f) > zeroCrossingsFor (0.5f, 0.0f)); // tenser rings higher than looser
}

TEST_CASE ("ResonatorPlateNode's quality structurally changes the real mode count used",
           "[engine][nodes][ResonatorPlateNode][PMCore]")
{
    ResonatorPlateNode low, high;
    low.setParameter ("resonator.plate.quality", 0.0f);  // low -> 8 modes
    high.setParameter ("resonator.plate.quality", 2.0f); // high -> 32 modes
    low.prepare ({ 44100.0, 512 });
    high.prepare ({ 44100.0, 512 });

    float outputsLow[2], outputsHigh[2];
    const float excited[7] = { 1.0f, 0.5f, 0.5f, 3.0f, 1.0f, 0.4f, 0.6f };
    low.processSample (excited, outputsLow);
    high.processSample (excited, outputsHigh);

    const float quiet[7] = { 0.0f, 0.5f, 0.5f, 3.0f, 1.0f, 0.4f, 0.6f };
    bool everDiffered = false;
    for (int i = 0; i < 1000; ++i)
    {
        low.processSample (quiet, outputsLow);
        high.processSample (quiet, outputsHigh);
        if (std::fabs (outputsLow[0] - outputsHigh[0]) > 1e-5f)
            everDiffered = true;
    }
    CHECK (everDiffered);
}

TEST_CASE ("ResonatorPlateNode's decay orders sustained energy: longer decay keeps more energy after the same time",
           "[engine][nodes][ResonatorPlateNode][PMCore]")
{
    ResonatorPlateNode shortDecay, longDecay;
    shortDecay.prepare ({ 44100.0, 512 });
    longDecay.prepare ({ 44100.0, 512 });

    float outputs[2];
    const float shortExcited[7] = { 1.0f, 0.5f, 0.5f, 0.1f, 1.0f, 0.4f, 0.6f };
    const float longExcited[7] = { 1.0f, 0.5f, 0.5f, 5.0f, 1.0f, 0.4f, 0.6f };
    shortDecay.processSample (shortExcited, outputs);
    longDecay.processSample (longExcited, outputs);

    const float shortQuiet[7] = { 0.0f, 0.5f, 0.5f, 0.1f, 1.0f, 0.4f, 0.6f };
    const float longQuiet[7] = { 0.0f, 0.5f, 0.5f, 5.0f, 1.0f, 0.4f, 0.6f };

    float rmsShort = 0.0f, rmsLong = 0.0f;
    const int total = 44100, window = 4410;
    for (int i = 0; i < total; ++i)
    {
        shortDecay.processSample (shortQuiet, outputs);
        const auto ls = outputs[0];
        longDecay.processSample (longQuiet, outputs);
        const auto ll = outputs[0];
        if (i >= total - window)
        {
            rmsShort += ls * ls;
            rmsLong += ll * ll;
        }
    }
    CHECK (rmsLong > rmsShort);
}

TEST_CASE ("ResonatorPlateNode pans deterministically across the stereo field (left and right genuinely differ)",
           "[engine][nodes][ResonatorPlateNode][PMCore]")
{
    ResonatorPlateNode node;
    node.prepare ({ 44100.0, 512 });

    float outputs[2];
    const float excited[7] = { 1.0f, 0.5f, 0.5f, 3.0f, 1.0f, 0.4f, 0.6f };
    node.processSample (excited, outputs);

    const float quiet[7] = { 0.0f, 0.5f, 0.5f, 3.0f, 1.0f, 0.4f, 0.6f };
    bool everDiffered = false;
    for (int i = 0; i < 1000; ++i)
    {
        node.processSample (quiet, outputs);
        if (std::fabs (outputs[0] - outputs[1]) > 1e-4f)
            everDiffered = true;
    }
    CHECK (everDiffered);
}

TEST_CASE ("ResonatorPlateNode stays finite and bounded over a long run at high quality",
           "[engine][nodes][ResonatorPlateNode][PMCore]")
{
    ResonatorPlateNode node;
    node.setParameter ("resonator.plate.quality", 2.0f); // high -> 32 modes
    node.prepare ({ 44100.0, 512 });

    float outputs[2];
    for (int i = 0; i < 44100 * 2; ++i)
    {
        const float inputs[7] = { i < 10 ? 0.9f : 0.0f, 0.1f, 0.9f, 4.0f, 0.1f, 0.5f, 0.5f };
        node.processSample (inputs, outputs);
        REQUIRE (std::isfinite (outputs[0]));
        REQUIRE (std::isfinite (outputs[1]));
        REQUIRE (std::fabs (outputs[0]) < 1000.0f);
        REQUIRE (std::fabs (outputs[1]) < 1000.0f);
    }
}

// ---- A real, live-crash bug: prepare() runs BEFORE setParameter() ----
//
// GraphCompiler.cpp's own real construction order for a freshly-created node
// is prepare() THEN setParameter() for every stored parameter (never the
// other way around) - confirmed by direct feedback that setting
// resonator.plate's own "quality" to High crashed the running Standalone
// app. Both tests below call prepare()/setParameter() in that EXACT real
// order (the opposite of every other test above in this file, which calls
// setParameter() first - a real gap in this file's own original coverage,
// not a second copy of an already-covered case).

TEST_CASE ("ResonatorPlateNode's state vectors are sized to the ceiling even when quality is raised AFTER prepare()",
           "[engine][nodes][ResonatorPlateNode][PMCore][regression]")
{
    ResonatorPlateNode node;
    node.prepare ({ 44100.0, 512 }); // quality is still its just-constructed default (Medium) here
    node.setParameter ("resonator.plate.quality", 2.0f); // High - the real order GraphCompiler.cpp uses

    // The real, live-reproduced crash: processSample() indexes state1/state2
    // up to activeModeCount-1 (31 at High). If the vectors were sized from
    // the live `quality` member INSIDE prepare() (the original bug), they'd
    // only have 16 elements here - this run would read/write out of bounds.
    float outputs[2];
    const float excited[7] = { 1.0f, 0.5f, 0.5f, 2.0f, 1.0f, 0.4f, 0.6f };
    const float quiet[7] = { 0.0f, 0.5f, 0.5f, 2.0f, 1.0f, 0.4f, 0.6f };
    node.processSample (excited, outputs);
    for (int i = 0; i < 1000; ++i)
    {
        node.processSample (quiet, outputs);
        REQUIRE (std::isfinite (outputs[0]));
        REQUIRE (std::isfinite (outputs[1]));
    }
}

TEST_CASE ("A real compiled graph placing resonator.plate at quality=High from the start does not crash",
           "[engine][PMCore][regression][integration]")
{
    // The literal reported scenario: a patch (or a live parameter edit,
    // which GraphEditController::recompileAndPublish() always turns into a
    // fresh node construction, CLAUDE.md's own documented behaviour) with
    // resonator.plate's own "resonator.plate.quality" parameter already set
    // to High (2.0) - exactly what a saved NodeInstance::parameters entry
    // looks like, exercising the real factory.create() -> prepare() ->
    // setParameter() order end to end, not a hand-sequenced unit test.
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    NodeGraph graph;
    graph.addNode ({ "plate", "resonator.plate", {},
                      { { "resonator.plate.quality", 2.0f }, { "resonator.plate.size", 0.2f } }, {} });
    graph.setOutput ("plate", "out");

    auto factory = buildDefaultNodeFactory();
    auto result = GraphCompiler::compile (graph, factory, { sampleRate, blockSize }, 1);
    REQUIRE (result.success);
    auto& plan = result.plan;

    const auto* leftPtr = plan.blockBuffers[(size_t) plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);
    for (int block = 0; block < 5; ++block)
    {
        plan.process (blockSize);
        for (int i = 0; i < blockSize; ++i)
            REQUIRE (std::isfinite (leftPtr[i]));
    }
}

// ---- The real cross-node feedback cycle: excite.mallet <-> resonator.string ----

TEST_CASE ("A real compiled graph closes excite.mallet<->resonator.string into a per-sample feedback region, and plays",
           "[engine][PMCore][integration]")
{
    // The exact case this whole batch's own intro confirmed GraphCompiler.cpp's
    // existing Tarjan SCC-based cycle detection already handles: resonator.string's
    // "motion" output feeds back into excite.mallet's own "feedback" input, closing
    // a literal 2-node graph cycle - the first production node pair to actually
    // exercise it (every earlier PM Core node's own feedback, where it has any, is
    // self-contained internal state, never a cross-node cycle).
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    NodeGraph graph;
    graph.addNode ({ "clock", "clock.pulse", {}, { { "clock.pulse.rate", 50.0f } }, {} });
    graph.addNode ({ "mallet", "excite.mallet", {}, { { "excite.mallet.velocity", 0.9f }, { "excite.mallet.stiffness", 0.6f } }, {} });
    graph.addNode ({ "string", "resonator.string", {}, { { "pitch", 69.0f }, { "resonator.string.decay", 2.0f } }, {} });
    graph.addConnection ({ "clock", "tick", "mallet", "trigger" });
    graph.addConnection ({ "mallet", "out", "string", "excite" });
    graph.addConnection ({ "string", "motion", "mallet", "feedback" });
    graph.setOutput ("string", "out");

    auto factory = buildDefaultNodeFactory();
    auto result = GraphCompiler::compile (graph, factory, { sampleRate, blockSize }, 1);
    REQUIRE (result.success);
    auto& plan = result.plan;

    // Confirm the cycle was genuinely detected and scheduled as a per-sample
    // region - not silently treated as acyclic (which would be a real,
    // previously-unexercised engine bug, not a passing edge case).
    bool hasPerSampleRegion = false;
    for (const auto& step : plan.steps)
        if (step.kind == ExecutionPlan::Step::Kind::PerSampleRegion)
            hasPerSampleRegion = true;
    REQUIRE (hasPerSampleRegion);

    const auto* outputPtr = plan.blockBuffers[(size_t) plan.finalOutputBufferIndex].getBlock().getChannelPointer (0);

    double sumSquares = 0.0;
    int totalSamples = 0;
    for (int block = 0; block < 10; ++block)
    {
        plan.process (blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            REQUIRE (std::isfinite (outputPtr[i]));
            REQUIRE (std::fabs (outputPtr[i]) < 1000.0f);
            sumSquares += (double) outputPtr[i] * (double) outputPtr[i];
            ++totalSamples;
        }
    }

    const auto rms = std::sqrt (sumSquares / (double) totalSamples);
    CHECK (rms > 0.0001); // real, audible output - the coupled system actually plays
}
