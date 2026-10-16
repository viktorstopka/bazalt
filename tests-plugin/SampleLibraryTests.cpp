// wiki/plans/Sampling.md 3a.1: the sample library, a missing sample, and a patch
// exported with its samples opening on "another machine" (an empty library).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "PluginProcessor.h"
#include "SampleLibrary.h"
#include "bazalt/engine/nodes/DataSampleNode.h"
#include "bazalt/engine/patch/PatchSerializer.h"

using namespace bazalt;

namespace
{
    /** A 0.25 s, 48 kHz stereo WAV of a 440 Hz sine, as file bytes. */
    juce::MemoryBlock sineWav()
    {
        juce::MemoryBlock bytes;
        {
            auto stream = std::make_unique<juce::MemoryOutputStream> (bytes, false);
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), 48000.0, 2, 24, {}, 0));
            REQUIRE (writer != nullptr);
            stream.release();
            juce::AudioBuffer<float> buffer (2, 12000);
            for (int i = 0; i < 12000; ++i)
                for (int c = 0; c < 2; ++c)
                    buffer.setSample (c, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * (float) i / 48000.0f));
            writer->writeFromAudioSampleBuffer (buffer, 0, 12000);
        }
        return bytes;
    }

    struct TempDir
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("bazalt-samples", "");
        TempDir() { dir.createDirectory(); }
        ~TempDir() { dir.deleteRecursively(); }
    };

    bazalt::engine::NodeGraph samplePatch (const juce::String& id)
    {
        bazalt::engine::NodeGraph g;
        bazalt::engine::NodeInstance node { "kick", "data.sample", {}, {}, {} };
        node.content = juce::JSON::parse (R"({ "sample": ")" + id + R"(", "name": "sine.wav", "end": 0.1 })");
        g.addNode (node);
        g.addNode ({ "masterOut", "io.output", {}, {}, {} });
        g.setOutput ("masterOut", "out");
        return g;
    }

    bazalt::engine::nodes::DataSampleNode* sampleNode (BazaltAudioProcessor& processor)
    {
        auto* plan = processor.getGlobalPlanSwapper().peekCurrentPlan();
        return plan != nullptr ? dynamic_cast<bazalt::engine::nodes::DataSampleNode*> (plan->getNodeById ("kick")) : nullptr;
    }
}

TEST_CASE ("A dropped file is decoded, stored once by its hash, and reloads from the library", "[plugin][sample][3a]")
{
    TempDir temp;
    bazalt::engine::SampleStore::instance().clearForTesting();
    const SampleLibrary library (temp.dir);
    const auto bytes = sineWav();

    const auto first = library.importFile ("sine.wav", bytes);
    INFO (first.errorMessage);
    REQUIRE (first.success);
    CHECK (first.source->channels == 2);
    CHECK (first.source->frames == 12000);
    CHECK (first.source->sampleRate == 48000.0);
    CHECK (first.source->at (100, 0) == Catch::Approx (0.5f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * 100.0f / 48000.0f)).margin (1.0e-4));
    CHECK (library.fileFor (first.source->id).existsAsFile());

    const auto again = library.importFile ("copy of sine.wav", bytes);
    CHECK (again.source->id == first.source->id);
    CHECK (temp.dir.findChildFiles (juce::File::findFiles, false, "*.flac").size() == 1);

    bazalt::engine::SampleStore::instance().clearForTesting();
    CHECK (library.ensureLoaded (first.source->id));
    CHECK (bazalt::engine::SampleStore::instance().find (first.source->id)->frames == 12000);
    CHECK_FALSE (library.ensureLoaded ("0000000000000000"));

    CHECK_FALSE (library.importFile ("notes.txt", juce::MemoryBlock ("hello", 5)).success);
    bazalt::engine::SampleStore::instance().clearForTesting();
}

TEST_CASE ("A patch whose sample is missing still loads; the node says so and publishes silence", "[plugin][sample][3a]")
{
    TempDir temp;
    bazalt::engine::SampleStore::instance().clearForTesting();
    BazaltAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    processor.getGraphEditController().setSampleLibraryForTesting (SampleLibrary (temp.dir));

    const auto result = processor.getGraphEditController().setGraph (samplePatch ("deadbeefdeadbeef"));
    INFO (result.errorMessage);
    REQUIRE (result.success);
    auto* node = sampleNode (processor);
    REQUIRE (node != nullptr);
    CHECK (node->isMissing());
    bazalt::engine::SampleStore::instance().clearForTesting();
}

TEST_CASE ("Export with samples embeds them; opening it with an empty library brings them back", "[plugin][sample][3a]")
{
    TempDir here, elsewhere;
    bazalt::engine::SampleStore::instance().clearForTesting();
    juce::String id;
    const auto exported = here.dir.getChildFile ("Shared.bazalt");
    {
        BazaltAudioProcessor processor;
        processor.prepareToPlay (48000.0, 512);
        auto& controller = processor.getGraphEditController();
        controller.setSampleLibraryForTesting (SampleLibrary (here.dir.getChildFile ("library")));
        const auto imported = controller.getSampleLibrary().importFile ("sine.wav", sineWav());
        REQUIRE (imported.success);
        id = imported.source->id;
        REQUIRE (controller.setGraph (samplePatch (id)).success);
        CHECK_FALSE (sampleNode (processor)->isMissing());

        const auto result = controller.exportPatchWithSamples (exported);
        INFO (result.errorMessage);
        REQUIRE (result.success);
        // An ordinary save keeps only the reference.
        CHECK (bazalt::engine::PatchDocument::fromNodeGraph (controller.getGraphForSaving()).assets.empty());
    }

    const auto parsed = bazalt::engine::parsePatchFromJson (exported.loadFileAsString());
    REQUIRE (parsed.success);
    CHECK (parsed.document.assets.count (juce::String (GraphEditController::sampleAssetPrefix) + id) == 1);

    // "Another machine": nothing loaded, an empty library.
    bazalt::engine::SampleStore::instance().clearForTesting();
    BazaltAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    auto& controller = processor.getGraphEditController();
    controller.setSampleLibraryForTesting (SampleLibrary (elsewhere.dir));
    REQUIRE (controller.setGraph (parsed.document.toNodeGraph()).success);
    auto* node = sampleNode (processor);
    REQUIRE (node != nullptr);
    CHECK_FALSE (node->isMissing());
    CHECK (controller.getSampleLibrary().fileFor (id).existsAsFile());
    CHECK (controller.getGraph().getAssets().empty()); // stripped back out: the DAW state stays small
    const auto* buffer = node->getDataPublisher()->getCurrentForAudioThread();
    REQUIRE (buffer != nullptr);
    CHECK (buffer->frames() == 4800); // the 0.1 s region
    bazalt::engine::SampleStore::instance().clearForTesting();
}

TEST_CASE ("A Sampler in a Voice plays its sample at the keys' pitches", "[plugin][sample][3a]")
{
    TempDir temp;
    bazalt::engine::SampleStore::instance().clearForTesting();
    BazaltAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    auto& c = processor.getGraphEditController();
    c.setSampleLibraryForTesting (SampleLibrary (temp.dir));
    const auto imported = c.getSampleLibrary().importFile ("sine.wav", sineWav());
    REQUIRE (imported.success);

    // The 440 Hz sine is A4 (69); the key an octave up should sound at 880 Hz.
    bazalt::engine::NodeGraph g;
    g.addNode ({ "notes", "io.noteIn", {}, {}, {} });
    g.addNode ({ "voice", "life.population", {}, {}, {} });
    bazalt::engine::NodeInstance sample { "sine", "data.sample", {}, {}, {} };
    sample.content = juce::JSON::parse (R"({ "sample": ")" + imported.source->id + R"(", "name": "sine.wav", "root": 69 })");
    g.addNode (sample);
    g.addNode ({ "sampler", "source.sampler", {}, {}, {} });
    g.addNode ({ "merge", "life.merge", {}, {}, {} });
    g.addNode ({ "masterOut", "io.output", {}, {}, {} });
    g.addConnection ({ "notes", "notes", "voice", "notes" });
    g.addConnection ({ "voice", "pitch", "sampler", "pitch" });
    g.addConnection ({ "sine", "sample", "sampler", "sample" });
    g.addConnection ({ "sampler", "out", "merge", "in" });
    g.addConnection ({ "merge", "out", "masterOut", "in" });
    g.setOutput ("masterOut", "out");
    const auto loaded = c.setGraph (g);
    INFO (loaded.errorMessage);
    REQUIRE (loaded.success);

    std::vector<float> out;
    juce::AudioBuffer<float> buffer (2, 512);
    for (int b = 0; b < 8; ++b)
    {
        buffer.clear();
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 81, (juce::uint8) 100), 0);
        processor.processBlock (buffer, midi);
        out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 512);
    }
    // The sample lasts 0.25 s at 440 Hz, so 0.125 s an octave up: read the first 0.1 s.
    out.resize (4800);
    const auto magnitude = [&out] (double hz)
    {
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < out.size(); ++i)
        {
            re += out[i] * std::cos (juce::MathConstants<double>::twoPi * hz * (double) i / 48000.0);
            im -= out[i] * std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / 48000.0);
        }
        return std::sqrt (re * re + im * im) / (double) out.size();
    };
    INFO ("880 " << magnitude (880.0) << " 440 " << magnitude (440.0));
    CHECK (magnitude (880.0) > 0.1);
    CHECK (magnitude (440.0) < 0.01);
}

TEST_CASE ("Freezing a Record of a pitch cable gives a Pitch sample in the library", "[plugin][sample][3a]")
{
    TempDir temp;
    bazalt::engine::SampleStore::instance().clearForTesting();
    BazaltAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    auto& c = processor.getGraphEditController();
    c.setSampleLibraryForTesting (SampleLibrary (temp.dir));

    bazalt::engine::NodeGraph g;
    bazalt::engine::NodeInstance pitch { "pitch", "math.frequencyToPitch", {}, {}, {} };
    pitch.parameters["frequency"] = 440.0f;
    g.addNode (pitch);
    bazalt::engine::NodeInstance record { "rec", "data.record", {}, {}, {} };
    record.parameters["data.record.length"] = 0.5f;
    record.parameters["data.record.channels"] = 0.0f;
    g.addNode (record);
    g.addNode ({ "masterOut", "io.output", {}, {}, {} });
    g.addConnection ({ "pitch", "pitch", "rec", "in" });
    g.addConnection ({ "rec", "position", "masterOut", "in" });
    g.setOutput ("masterOut", "out");
    const auto loaded = c.setGraph (g);
    INFO (loaded.errorMessage);
    REQUIRE (loaded.success);

    CHECK (c.freezeRecord ("rec").errorMessage == "Nothing recorded yet");

    juce::AudioBuffer<float> buffer (2, 512);
    for (int b = 0; b < 20; ++b) // ~0.21 s
    {
        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);
    }
    CHECK (c.recordedSeconds ("rec") == Catch::Approx (20.0 * 512.0 / 48000.0));

    const auto frozen = c.freezeRecord ("rec");
    INFO (frozen.errorMessage);
    REQUIRE (frozen.success);
    CHECK (frozen.quantity == (int) bazalt::engine::Quantity::Pitch);
    CHECK (frozen.seconds == Catch::Approx (20.0 * 512.0 / 48000.0));
    const auto source = bazalt::engine::SampleStore::instance().find (frozen.sampleId);
    REQUIRE (source != nullptr);
    CHECK (source->channels == 1);
    CHECK (source->at (100, 0) == Catch::Approx (69.0f).margin (1.0e-3));
    CHECK (c.getSampleLibrary().fileFor (frozen.sampleId).existsAsFile());

    // Recording goes on after a Freeze.
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);
    CHECK (c.recordedSeconds ("rec") == Catch::Approx (21.0 * 512.0 / 48000.0));

    // The Sample node a Freeze makes keeps the meaning.
    bazalt::engine::SampleRegion region;
    region.sampleId = frozen.sampleId;
    region.quantity = frozen.quantity;
    const auto published = bazalt::engine::buildSampleBuffer (source.get(), bazalt::engine::SampleRegion::fromVar (region.toVar()),
                                                             (uint8_t) bazalt::engine::Quantity::Audio);
    CHECK (published->sampleMeta.channelQuantities.at (0) == (uint8_t) bazalt::engine::Quantity::Pitch);
}

TEST_CASE ("A file arrives from the WebView as standard base64", "[plugin][sample][3a]")
{
    // What FileReader.readAsDataURL gives (after "data:...;base64,"): RFC 4648.
    juce::MemoryBlock man ("Man", 3);
    CHECK (encodeBase64 (man) == "TWFu");
    juce::MemoryBlock decoded;
    REQUIRE (decodeBase64 ("TWFu", decoded));
    CHECK (decoded == man);

    TempDir temp;
    bazalt::engine::SampleStore::instance().clearForTesting();
    const SampleLibrary library (temp.dir);
    const auto wav = sineWav();
    juce::MemoryBlock bytes;
    REQUIRE (decodeBase64 (encodeBase64 (wav), bytes));
    CHECK (bytes == wav);
    const auto imported = library.importFile ("sine.wav", bytes);
    INFO (imported.errorMessage);
    CHECK (imported.success);
}
