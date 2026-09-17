#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/nodes/ConstantNode.h"
#include "bazalt/engine/nodes/RerouteNode.h"
#include "bazalt/engine/nodes/MapNode.h"
#include "bazalt/engine/nodes/AddNode.h"
#include "bazalt/engine/nodes/MultiplyNode.h"
#include "bazalt/engine/nodes/ListenNode.h"
#include "bazalt/engine/nodes/OutputNode.h"
#include "bazalt/engine/nodes/VoiceSumNode.h"
#include <algorithm>

using namespace bazalt::engine;
using namespace bazalt::engine::nodes;

TEST_CASE ("ConstantNode outputs whatever its value parameter is set to", "[engine][nodes][util]")
{
    ConstantNode node;
    node.setParameter ("util.constant.value", 3.5f);

    float out = 0.0f;
    node.processSample (nullptr, &out);
    CHECK (out == 3.5f);
}

TEST_CASE ("RerouteNode passes its input straight through", "[engine][nodes][util]")
{
    RerouteNode node;
    const float in = -0.75f;
    float out = 0.0f;
    node.processSample (&in, &out);
    CHECK (out == in);
}

TEST_CASE ("MapNode remaps a 0..1 input onto its min/max range, clamped", "[engine][nodes][util]")
{
    MapNode node;
    node.setParameter ("util.map.min", 200.0f);
    node.setParameter ("util.map.max", 8000.0f);

    auto mapOf = [&] (float normalized)
    {
        float out = 0.0f;
        node.processSample (&normalized, &out);
        return out;
    };

    CHECK (mapOf (0.0f) == 200.0f);
    CHECK (mapOf (1.0f) == 8000.0f);
    CHECK (mapOf (0.5f) == 4100.0f);
    CHECK (mapOf (-1.0f) == 200.0f);  // clamped
    CHECK (mapOf (2.0f) == 8000.0f); // clamped
}

TEST_CASE ("AddNode and MultiplyNode compute a+b and a*b", "[engine][nodes][util]")
{
    AddNode add;
    const float addInputs[2] = { 2.0f, 5.0f };
    float addOut = 0.0f;
    add.processSample (addInputs, &addOut);
    CHECK (addOut == 7.0f);

    MultiplyNode multiply;
    const float mulInputs[2] = { 2.0f, 5.0f };
    float mulOut = 0.0f;
    multiply.processSample (mulInputs, &mulOut);
    CHECK (mulOut == 10.0f);
}

TEST_CASE ("ListenNode accepts an input and produces no output without asserting or crashing",
           "[engine][nodes][util]")
{
    ListenNode node;
    CHECK (node.getNumOutputPorts() == 0);
    const float in = 1.0f;
    node.processSample (&in, nullptr); // must not touch a null outputs pointer
}

TEST_CASE ("OutputNode is a unity pass-through", "[engine][nodes][util]")
{
    OutputNode node;
    const float in = 0.42f;
    float out = 0.0f;
    node.processSample (&in, &out);
    CHECK (out == in);
}

TEST_CASE ("VoiceSumNode outputs its externally-supplied block, ignoring its (Silence) graph input",
           "[engine][nodes][util][NODE_EDITOR]")
{
    VoiceSumNode node;
    CHECK_FALSE (node.supportsPerSample()); // a domain seam, never legally inside a feedback cycle

    constexpr int numSamples = 4;
    const float externalBlock[numSamples] = { 0.1f, 0.2f, 0.3f, 0.4f };
    float output[numSamples] = {};
    const float silence[numSamples] = {};

    node.setExternalBlock (externalBlock, numSamples);

    const float* inputs[1] = { silence };
    float* outputs[1] = { output };
    node.processBlock (inputs, outputs, numSamples);

    for (int i = 0; i < numSamples; ++i)
        CHECK (output[i] == externalBlock[i]);
}

TEST_CASE ("VoiceSumNode outputs silence if asked to process a block size that doesn't match what was set",
           "[engine][nodes][util][NODE_EDITOR]")
{
    VoiceSumNode node;
    constexpr int setNumSamples = 4;
    const float externalBlock[setNumSamples] = { 1.0f, 1.0f, 1.0f, 1.0f };
    node.setExternalBlock (externalBlock, setNumSamples);

    constexpr int processNumSamples = 8;
    float output[processNumSamples];
    std::fill (std::begin (output), std::end (output), 1.0f);
    const float silence[processNumSamples] = {};

    const float* inputs[1] = { silence };
    float* outputs[1] = { output };
    node.processBlock (inputs, outputs, processNumSamples);

    for (int i = 0; i < processNumSamples; ++i)
        CHECK (output[i] == 0.0f);
}
