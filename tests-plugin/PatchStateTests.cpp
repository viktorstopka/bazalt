#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "MacroParameters.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/patch/PatchSerializer.h"
#include <algorithm>
#include <cmath>

using namespace bazalt;

namespace
{
    float rms (const juce::AudioBuffer<float>& buffer, int channel)
    {
        double sumSquares = 0.0;
        const auto* data = buffer.getReadPointer (channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            sumSquares += (double) data[i] * (double) data[i];
        return (float) std::sqrt (sumSquares / buffer.getNumSamples());
    }
}

TEST_CASE ("Plugin state round-trips macro values exactly through getStateInformation/setStateInformation",
           "[plugin][patch]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& parameters = processor.getParameters();
    REQUIRE (parameters.size() >= MacroParameters::numMacros);

    *dynamic_cast<juce::AudioParameterFloat*> (parameters[0]) = 0.35f;
    *dynamic_cast<juce::AudioParameterFloat*> (parameters[1]) = 0.812345f;
    *dynamic_cast<juce::AudioParameterFloat*> (parameters[2]) = 1.0f;

    juce::MemoryBlock state;
    processor.getStateInformation (state);

    BazaltAudioProcessor reloaded;
    reloaded.prepareToPlay (44100.0, 512);
    reloaded.setStateInformation (state.getData(), (int) state.getSize());

    auto& reloadedParameters = reloaded.getParameters();

    for (int i = 0; i < MacroParameters::numMacros; ++i)
    {
        auto* original = dynamic_cast<juce::AudioParameterFloat*> (parameters[i]);
        auto* restored = dynamic_cast<juce::AudioParameterFloat*> (reloadedParameters[i]);

        REQUIRE (original != nullptr);
        REQUIRE (restored != nullptr);
        CHECK (restored->get() == original->get()); // bit-identical, not approximate
    }
}

TEST_CASE ("Plugin state's graph/macro content round-trips exactly (meta.modifiedAtMs deliberately excluded)",
           "[plugin][patch]")
{
    // meta.modifiedAtMs is stamped with juce::Time::getCurrentTime() on
    // every getStateAsJson() call by design (it reflects "when this was
    // serialized", not stored state) — comparing it across two separate
    // calls would spuriously differ by design, not by bug. The exit
    // criterion this test earns its keep against is specifically
    // "parameter/macro state", so that's what gets compared here.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    const auto json = processor.getStateAsJson();
    REQUIRE (json.isNotEmpty());

    BazaltAudioProcessor reloaded;
    reloaded.prepareToPlay (44100.0, 512);
    REQUIRE (reloaded.loadStateFromJson (json));

    const auto original = processor.getCurrentPatchDocument();
    const auto restored = reloaded.getCurrentPatchDocument();

    REQUIRE (restored.nodes.size() == original.nodes.size());
    for (size_t i = 0; i < original.nodes.size(); ++i)
    {
        CHECK (restored.nodes[i].id == original.nodes[i].id);
        CHECK (restored.nodes[i].type == original.nodes[i].type);

        for (const auto& [paramId, value] : original.nodes[i].parameters)
        {
            REQUIRE (restored.nodes[i].parameters.count (paramId) == 1);
            CHECK (restored.nodes[i].parameters.at (paramId) == value);
        }
    }

    REQUIRE (restored.connections.size() == original.connections.size());
    for (size_t i = 0; i < original.connections.size(); ++i)
    {
        CHECK (restored.connections[i].fromNodeId == original.connections[i].fromNodeId);
        CHECK (restored.connections[i].toNodeId == original.connections[i].toNodeId);
    }

    // macroMappings is no longer a persisted PatchDocument field
    // (wiki/plans/UtilMacro.md, schema v7) — it's derived fresh from the
    // graph's own util.macro nodes on every recompile, already covered by
    // the nodes/connections round-trip above. Only the raw per-slot values
    // still need their own check.
    REQUIRE (restored.macroValues.size() == original.macroValues.size());
    for (size_t i = 0; i < original.macroValues.size(); ++i)
        CHECK (restored.macroValues[i] == original.macroValues[i]);
}

TEST_CASE ("Loading malformed state data is rejected without crashing or corrupting the processor",
           "[plugin][patch]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    const char garbage[] = "not json at all { [ }";
    processor.setStateInformation (garbage, (int) sizeof (garbage));

    juce::AudioBuffer<float> buffer (2, 64);
    juce::MidiBuffer midi;
    buffer.clear();
    processor.processBlock (buffer, midi);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            REQUIRE (std::isfinite (buffer.getSample (ch, i)));
}

// Closes a real test gap found during a post-ship sweep (wiki/plans/UtilMacro.md):
// the two tests above never actually exercised a util.macro node (both run against
// the default master-out-only graph) - the exact path a human hits the first time
// they save/reload a patch containing macros had never been run by CI.

TEST_CASE ("A saved patch with multiple claimed util.macro nodes round-trips exactly, and host "
           "automation reaches each real target through a real processBlock after reload",
           "[plugin][patch][macro]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.setGraph (bazalt::engine::buildVoiceProofGraph()).success);

    REQUIRE (controller.createMacro ("cutoffMacro", 0.0f, 0.0f, 3, 200.0f, 12000.0f, false, 1, "Hz", 0.5f).success);
    REQUIRE (controller.connectWithAutoAdapt ("cutoffMacro", "out", "svf", "filter.svf.cutoff").success);
    REQUIRE (controller.createMacro ("resMacro", 200.0f, 0.0f, 9, 0.3f, 4.0f, false, 0, "", 0.5f).success);
    REQUIRE (controller.connectWithAutoAdapt ("resMacro", "out", "svf", "filter.svf.resonance").success);

    const auto json = processor.getStateAsJson();
    REQUIRE (json.isNotEmpty());

    BazaltAudioProcessor reloaded;
    reloaded.prepareToPlay (44100.0, 512);
    REQUIRE (reloaded.loadStateFromJson (json));

    const auto doc = reloaded.getCurrentPatchDocument();
    const auto findNode = [&] (const juce::String& id)
    {
        return std::find_if (doc.nodes.begin(), doc.nodes.end(), [&] (const auto& n) { return n.id == id; });
    };

    const auto cutoffNode = findNode ("cutoffMacro");
    REQUIRE (cutoffNode != doc.nodes.end());
    CHECK (cutoffNode->parameters.at ("util.macro.slot") == 3.0f);
    CHECK (cutoffNode->parameters.at ("util.macro.min") == 200.0f);
    CHECK (cutoffNode->parameters.at ("util.macro.max") == 12000.0f);

    const auto resNode = findNode ("resMacro");
    REQUIRE (resNode != doc.nodes.end());
    CHECK (resNode->parameters.at ("util.macro.slot") == 9.0f);
    CHECK (resNode->parameters.at ("util.macro.min") == 0.3f);
    CHECK (resNode->parameters.at ("util.macro.max") == 4.0f);

    // Host automation on each slot must reach its real target through a real
    // processBlock() post-load - this exercises setGraph() ->
    // recompileAndPublish() -> deriveMacroMappings() actually running after a
    // LOAD, not just after a live command sequence (every other macro test
    // in this codebase only ever checks the live-command path).
    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    reloaded.processBlock (buffer, noteOn);

    auto settle = [&] (int slot, float raw)
    {
        reloaded.getMacroParameter (slot).setValueNotifyingHost (raw);
        juce::AudioBuffer<float> settled (2, 512);
        for (int block = 0; block < 10; ++block)
        {
            settled.clear();
            juce::MidiBuffer empty;
            reloaded.processBlock (settled, empty);
        }
        return rms (settled, 0);
    };

    const auto rmsLowCutoff = settle (3, 0.0f);
    const auto rmsHighCutoff = settle (3, 1.0f);
    CHECK (rmsHighCutoff > rmsLowCutoff * 1.2f);
}

TEST_CASE ("Loading a saved patch where two util.macro nodes claim the same slot is rejected, "
           "leaving the processor's prior graph intact",
           "[plugin][patch][macro]")
{
    // The equivalent two-colliding-macros scenario was previously only ever
    // tested via live GraphEditController commands - this is the same check
    // through loadStateFromJson() instead, the path a hand-edited or
    // corrupted saved file would actually hit.
    BazaltAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    const auto originalDoc = processor.getCurrentPatchDocument();

    const juce::String colliding = R"json({
        "schemaVersion": 7,
        "nodes": [
            { "id": "m1", "type": "util.macro", "position": { "x": 0.0, "y": 0.0 }, "parameters": { "util.macro.slot": 4.0 }, "properties": {} },
            { "id": "m2", "type": "util.macro", "position": { "x": 100.0, "y": 0.0 }, "parameters": { "util.macro.slot": 4.0 }, "properties": {} },
            { "id": "out", "type": "io.output", "position": { "x": 200.0, "y": 0.0 }, "parameters": {}, "properties": {} }
        ],
        "connections": [],
        "outputNodeId": "out",
        "outputPortId": "out",
        "macroValues": [],
        "view": { "panX": 0.0, "panY": 0.0, "zoom": 1.0 },
        "meta": { "name": "", "author": "", "createdAtMs": 0, "modifiedAtMs": 0 }
    })json";

    CHECK_FALSE (processor.loadStateFromJson (colliding));

    const auto afterDoc = processor.getCurrentPatchDocument();
    REQUIRE (afterDoc.nodes.size() == originalDoc.nodes.size());
    for (size_t i = 0; i < originalDoc.nodes.size(); ++i)
        CHECK (afterDoc.nodes[i].id == originalDoc.nodes[i].id);
}

// wiki/plans/DataAndWavetable.md §2: every factory patch the UI ships still
// loads through the whole migration chain, compiles, and plays a note — the
// sweep's removed and merged nodes must never strand an older patch.
TEST_CASE ("The shipped factory patches load, compile and sound after every migration",
           "[plugin][PatchState][sweep]")
{
    const auto dir = juce::File (BAZALT_FACTORY_PATCH_DIR);
    REQUIRE (dir.isDirectory());
    const auto files = dir.findChildFiles (juce::File::findFiles, false, "*.json");
    REQUIRE_FALSE (files.isEmpty());

    for (const auto& file : files)
    {
        INFO (file.getFileName());
        BazaltAudioProcessor processor;
        processor.prepareToPlay (44100.0, 512);

        const auto parsed = bazalt::engine::parsePatchFromJson (file.loadFileAsString());
        REQUIRE (parsed.success);
        const auto result = processor.getGraphEditController().setGraph (parsed.document.toNodeGraph());
        INFO (result.errorMessage);
        REQUIRE (result.success);

        juce::MidiBuffer noteOn;
        noteOn.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
        juce::AudioBuffer<float> buffer (2, 512);
        float peak = 0.0f;
        for (int block = 0; block < 40; ++block)
        {
            buffer.clear();
            juce::MidiBuffer midi;
            if (block == 0)
                midi = noteOn;
            processor.processBlock (buffer, midi);
            peak = juce::jmax (peak, buffer.getMagnitude (0, 0, buffer.getNumSamples()));
        }
        CHECK (peak > 1.0e-4f);
    }
}
