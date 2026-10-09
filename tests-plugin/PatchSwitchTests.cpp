// Switching patches the way the patch menu does (graphRestoreSnapshot ->
// GraphEditController::setGraph) must leave nothing of the previous patch
// sounding: a MIDI note after a switch plays exactly what a fresh instance
// with only the new patch plays.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "PluginProcessor.h"
#include "bazalt/engine/patch/PatchSerializer.h"
#include <cmath>

using namespace bazalt;

namespace
{
    // A trigger-button patch: macro -> Gate Length -> Envelope -> Triangle
    // amplitude -> Master Out. Mono (no Voice), silent until the button.
    const char* buttonPatch = R"json({
      "schemaVersion": 16,
      "nodes": [
        { "id": "masterOut", "type": "io.output", "position": { "x": 0, "y": 0 }, "parameters": {}, "properties": {} },
        { "id": "node10", "type": "util.macro", "position": { "x": 0, "y": 0 },
          "parameters": { "util.macro.slot": 0, "util.macro.type": 2 }, "properties": {} },
        { "id": "node11", "type": "time.gateLength", "position": { "x": 0, "y": 0 }, "parameters": { "length": 1.0 }, "properties": {} },
        { "id": "node12", "type": "source.envelope", "position": { "x": 0, "y": 0 }, "parameters": {}, "properties": {} },
        { "id": "node13", "type": "source.triangle", "position": { "x": 0, "y": 0 },
          "parameters": { "source.triangle.frequency": 3000 }, "properties": {} }
      ],
      "connections": [
        { "fromNodeId": "node10", "fromPortId": "out", "toNodeId": "node11", "toPortId": "trigger" },
        { "fromNodeId": "node11", "fromPortId": "gate", "toNodeId": "node12", "toPortId": "gate" },
        { "fromNodeId": "node12", "fromPortId": "out", "toNodeId": "node13", "toPortId": "source.triangle.amplitude" },
        { "fromNodeId": "node13", "fromPortId": "out", "toNodeId": "masterOut", "toPortId": "in" }
      ],
      "outputNodeId": "masterOut", "outputPortId": "out"
    })json";

    const char* emptyPatch = R"json({
      "schemaVersion": 16,
      "nodes": [ { "id": "masterOut", "type": "io.output", "position": { "x": 640, "y": 360 }, "parameters": {}, "properties": {} } ],
      "connections": [], "outputNodeId": "masterOut", "outputPortId": "out"
    })json";

    // Note In -> Voice -> Pitch to Frequency -> Sine; Voice gate -> Envelope;
    // Sine x Envelope -> Merge -> Master Out. Ids continue the editor's
    // counter, the way a new patch built after the first one would.
    const char* voicePatch = R"json({
      "schemaVersion": 16,
      "nodes": [
        { "id": "masterOut", "type": "io.output", "position": { "x": 0, "y": 0 }, "parameters": {}, "properties": {} },
        { "id": "node14", "type": "io.noteIn", "position": { "x": 0, "y": 0 }, "parameters": {}, "properties": {} },
        { "id": "node15", "type": "life.voice", "position": { "x": 0, "y": 0 }, "parameters": {}, "properties": {} },
        { "id": "node16", "type": "math.pitchToFrequency", "position": { "x": 0, "y": 0 }, "parameters": {}, "properties": {} },
        { "id": "node17", "type": "source.sine", "position": { "x": 0, "y": 0 }, "parameters": {}, "properties": {} },
        { "id": "node18", "type": "source.envelope", "position": { "x": 0, "y": 0 }, "parameters": {}, "properties": {} },
        { "id": "node19", "type": "math.multiply", "position": { "x": 0, "y": 0 }, "parameters": {}, "properties": {} },
        { "id": "node20", "type": "life.merge", "position": { "x": 0, "y": 0 }, "parameters": {}, "properties": {} }
      ],
      "connections": [
        { "fromNodeId": "node14", "fromPortId": "notes", "toNodeId": "node15", "toPortId": "spawn" },
        { "fromNodeId": "node15", "fromPortId": "pitch", "toNodeId": "node16", "toPortId": "pitch" },
        { "fromNodeId": "node16", "fromPortId": "frequency", "toNodeId": "node17", "toPortId": "source.sine.frequency" },
        { "fromNodeId": "node15", "fromPortId": "gate", "toNodeId": "node18", "toPortId": "gate" },
        { "fromNodeId": "node17", "fromPortId": "out", "toNodeId": "node19", "toPortId": "in.0" },
        { "fromNodeId": "node18", "fromPortId": "out", "toNodeId": "node19", "toPortId": "in.1" },
        { "fromNodeId": "node19", "fromPortId": "out", "toNodeId": "node20", "toPortId": "in" },
        { "fromNodeId": "node20", "fromPortId": "out", "toNodeId": "masterOut", "toPortId": "in" }
      ],
      "outputNodeId": "masterOut", "outputPortId": "out"
    })json";

    bool load (BazaltAudioProcessor& processor, const char* json)
    {
        const auto parsed = bazalt::engine::parsePatchFromJson (json);
        REQUIRE (parsed.success);
        const auto result = processor.getGraphEditController().setGraph (parsed.document.toNodeGraph());
        INFO (result.errorMessage);
        return result.success;
    }

    std::vector<float> render (BazaltAudioProcessor& processor, int blocks, bool noteOnFirst)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, 512);
        for (int b = 0; b < blocks; ++b)
        {
            buffer.clear();
            juce::MidiBuffer midi;
            if (noteOnFirst && b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            processor.processBlock (buffer, midi);
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 512);
        }
        return out;
    }

    double magnitudeAt (const std::vector<float>& signal, double frequency, double sampleRate)
    {
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < signal.size(); ++i)
        {
            const auto angle = juce::MathConstants<double>::twoPi * frequency * (double) i / sampleRate;
            re += signal[i] * std::cos (angle);
            im -= signal[i] * std::sin (angle);
        }
        return std::sqrt (re * re + im * im) / (double) signal.size();
    }

    float maxDifference (const std::vector<float>& a, const std::vector<float>& b)
    {
        float worst = 0.0f;
        for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
            worst = std::max (worst, std::abs (a[i] - b[i]));
        return worst;
    }
}

TEST_CASE ("After a button patch and Empty, a new Voice patch plays only itself", "[plugin][PatchSwitch]")
{
    BazaltAudioProcessor reference;
    reference.prepareToPlay (48000.0, 512);
    REQUIRE (load (reference, voicePatch));
    const auto expected = render (reference, 20, true);

    BazaltAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    REQUIRE (load (processor, buttonPatch));
    render (processor, 4, false);
    REQUIRE (load (processor, emptyPatch));
    render (processor, 4, false);
    REQUIRE (load (processor, voicePatch));
    const auto actual = render (processor, 20, true);

    CHECK (maxDifference (expected, actual) < 1.0e-3f);
}

TEST_CASE ("A Voice patch, then a button patch, then Empty, then a Voice patch again", "[plugin][PatchSwitch]")
{
    BazaltAudioProcessor reference;
    reference.prepareToPlay (48000.0, 512);
    REQUIRE (load (reference, voicePatch));
    const auto expected = render (reference, 20, true);

    BazaltAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    REQUIRE (load (processor, voicePatch));
    render (processor, 20, true);
    REQUIRE (load (processor, buttonPatch));
    render (processor, 4, false);
    REQUIRE (load (processor, emptyPatch));
    const auto silentAfterEmpty = render (processor, 8, true); // MIDI into an empty patch: silence
    float peak = 0.0f;
    for (const auto v : silentAfterEmpty)
        peak = std::max (peak, std::abs (v));
    CHECK (peak < 1.0e-6f);
    REQUIRE (load (processor, voicePatch));
    const auto actual = render (processor, 20, true);
    // The note may land on another voice than in a fresh instance, so compare
    // what is heard, not sample by sample: the new patch at full level, and
    // nothing of the button patch's 3 kHz triangle.
    const auto heard = magnitudeAt (actual, 261.63, 48000.0);
    CHECK (heard == Catch::Approx (magnitudeAt (expected, 261.63, 48000.0)).epsilon (0.05));
    CHECK (magnitudeAt (actual, 3000.0, 48000.0) < 0.001);
}

TEST_CASE ("Patches built and torn down command by command leave nothing behind", "[plugin][PatchSwitch]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    auto& c = processor.getGraphEditController();
    auto step = [&] (GraphEditController::CommandResult r)
    {
        INFO (r.errorMessage);
        REQUIRE (r.success);
        render (processor, 2, false);
    };

    // 1. A voice patch, built one command at a time, and played.
    step (c.addNode ("io.noteIn", "n1", 0, 0));
    step (c.addNode ("life.voice", "n2", 0, 0));
    step (c.connect ("n1", "notes", "n2", "spawn"));
    step (c.addNode ("source.triangle", "n3", 0, 0));
    step (c.setParameterValue ("n3", "source.triangle.frequency", 3000.0f));
    step (c.addNode ("source.envelope", "n4", 0, 0));
    step (c.connect ("n2", "gate", "n4", "gate"));
    step (c.connect ("n4", "out", "n3", "source.triangle.amplitude"));
    step (c.addNode ("life.merge", "n5", 0, 0));
    step (c.connect ("n3", "out", "n5", "in"));
    step (c.connect ("n5", "out", "masterOut", "in"));
    render (processor, 10, true);

    // 2. Turned into a button patch: the Voice goes, a Macro button drives it.
    step (c.deleteNode ("n5"));
    step (c.deleteNode ("n2"));
    step (c.deleteNode ("n1"));
    step (c.connect ("n3", "out", "masterOut", "in"));
    step (c.addNode ("time.gateLength", "n6", 0, 0));
    step (c.connect ("n6", "gate", "n4", "gate"));

    // 3. Empty.
    REQUIRE (load (processor, emptyPatch));
    render (processor, 4, false);

    // 4. A new Voice patch with a 220 Hz-ish sine, built command by command.
    step (c.addNode ("io.noteIn", "n7", 0, 0));
    step (c.addNode ("life.voice", "n8", 0, 0));
    step (c.connect ("n7", "notes", "n8", "spawn"));
    step (c.addNode ("math.pitchToFrequency", "n9", 0, 0));
    step (c.connect ("n8", "pitch", "n9", "pitch"));
    step (c.addNode ("source.sine", "n10", 0, 0));
    step (c.connect ("n9", "frequency", "n10", "source.sine.frequency"));
    step (c.addNode ("source.envelope", "n11", 0, 0));
    step (c.connect ("n8", "gate", "n11", "gate"));
    step (c.addNode ("math.multiply", "n12", 0, 0));
    step (c.connect ("n10", "out", "n12", "in.0"));
    step (c.connect ("n11", "out", "n12", "in.1"));
    step (c.addNode ("life.merge", "n13", 0, 0));
    step (c.connect ("n12", "out", "n13", "in"));
    step (c.connect ("n13", "out", "masterOut", "in"));

    const auto out = render (processor, 20, true);
    const auto newPatch = magnitudeAt (out, 261.63, 48000.0);
    const auto oldPatch = magnitudeAt (out, 3000.0, 48000.0);
    INFO ("new " << newPatch << " old " << oldPatch);
    CHECK (newPatch > 0.05);
    CHECK (oldPatch < 0.001);
}

TEST_CASE ("Edits while the global plan is not being played still reach it", "[plugin][PatchSwitch]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    auto& c = processor.getGraphEditController();
    auto step = [&] (GraphEditController::CommandResult r)
    {
        INFO (r.errorMessage);
        REQUIRE (r.success);
        render (processor, 2, false);
    };

    // The button patch plays from the global plan.
    REQUIRE (load (processor, buttonPatch));
    render (processor, 4, false);

    // Voices straight into Master Out (no Merge): the output is per-voice, the
    // global plan is compiled (it holds the Macro) but never played.
    REQUIRE (load (processor, emptyPatch));
    step (c.addNode ("io.noteIn", "v1", 0, 0));
    step (c.addNode ("life.voice", "v2", 0, 0));
    step (c.connect ("v1", "notes", "v2", "spawn"));
    step (c.addNode ("source.sine", "v3", 0, 0));
    step (c.connect ("v3", "out", "masterOut", "in"));
    step (c.addNode ("source.envelope", "v4", 0, 0));
    step (c.connect ("v2", "gate", "v4", "gate"));
    step (c.connect ("v4", "out", "v3", "source.sine.amplitude"));
    step (c.addNode ("util.constant", "k1", 0, 0));
    for (int i = 0; i < 8; ++i)
        step (c.setParameterValue ("k1", "util.constant.value", (float) i));

    // Now through a Merge: the global plan plays again, and must be this one.
    step (c.addNode ("life.merge", "v5", 0, 0));
    step (c.connect ("v3", "out", "v5", "in"));
    step (c.connect ("v5", "out", "masterOut", "in"));

    const auto out = render (processor, 20, true);
    const auto sine = magnitudeAt (out, 440.0, 48000.0);
    const auto button = magnitudeAt (out, 3000.0, 48000.0);
    INFO ("sine " << sine << " button patch " << button);
    CHECK (sine > 0.05);
    CHECK (button < 0.001);
    CHECK (processor.getGlobalPlanSwapper().getNumOccupiedSlots() <= 2);
}

TEST_CASE ("Every shipped factory patch loads and compiles", "[plugin][PatchSwitch][factory]")
{
    const auto dir = juce::File (BAZALT_FACTORY_PATCH_DIR);
    REQUIRE (dir.isDirectory());
    const auto files = dir.findChildFiles (juce::File::findFiles, false, "*.bazalt");
    REQUIRE (files.size() >= 3);

    BazaltAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    for (const auto& file : files)
    {
        INFO (file.getFileName());
        const auto parsed = bazalt::engine::parsePatchFromJson (file.loadFileAsString());
        REQUIRE (parsed.success);
        const auto result = processor.getGraphEditController().setGraph (parsed.document.toNodeGraph());
        INFO (result.errorMessage);
        CHECK (result.success);
        const auto out = render (processor, 8, true);
        for (const auto v : out)
            REQUIRE (std::isfinite (v));
    }
}
