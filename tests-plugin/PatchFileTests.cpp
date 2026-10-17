// wiki/plans/Release.md R0.1b: the .bazalt file is one zip — patch.json plus
// each asset's raw bytes — and the DAW state is gzip-compressed JSON.
#include <catch2/catch_test_macros.hpp>
#include "PatchFile.h"
#include "PluginProcessor.h"
#include "UserPatchLibrary.h"
#include "bazalt/engine/patch/PatchSerializer.h"

using namespace bazalt;

namespace
{
    struct TempDir
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("bazalt-patchfile", "");
        TempDir() { dir.createDirectory(); }
        ~TempDir() { dir.deleteRecursively(); }
    };

    /** A patch with one image asset whose bytes are `image`. */
    bazalt::engine::PatchDocument patchWithImage (const juce::MemoryBlock& image)
    {
        bazalt::engine::PatchDocument doc;
        doc.nodes.push_back ({ "masterOut", "io.output", {}, {}, {} });
        doc.outputNodeId = "masterOut";
        doc.outputPortId = "out";
        doc.assets["img:1"] = { "image/png", encodeBase64 (image) };
        return doc;
    }

    juce::MemoryBlock someBytes (int n)
    {
        juce::MemoryBlock b;
        for (int i = 0; i < n; ++i)
            b.append (std::array<char, 1> { (char) (i * 37 + 11) }.data(), 1);
        return b;
    }
}

TEST_CASE ("A saved patch is one zip: patch.json plus each asset's raw bytes", "[plugin][patch][R0]")
{
    TempDir temp;
    const UserPatchLibrary library (temp.dir);
    const auto image = someBytes (5000);
    juce::String error;
    const auto fileName = library.save ("With a picture", patchWithImage (image), error);
    INFO (error);
    REQUIRE (fileName.isNotEmpty());

    const auto file = temp.dir.getChildFile (fileName);
    juce::MemoryBlock bytes;
    REQUIRE (file.loadFileAsData (bytes));
    CHECK (PatchFile::isZip (bytes));

    juce::ZipFile zip (file);
    REQUIRE (zip.getIndexOfFileName ("patch.json") >= 0);
    const auto assetIndex = zip.getIndexOfFileName ("assets/img-1.png");
    REQUIRE (assetIndex >= 0);
    std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (assetIndex));
    juce::MemoryBlock stored;
    stream->readIntoMemoryBlock (stored);
    CHECK (stored == image); // raw, not base64
    CHECK (bytes.getSize() < 5000 * 4 / 3); // smaller than the base64 alone would be

    // Loading gives back the app's JSON, the asset as base64 again.
    const auto parsed = bazalt::engine::parsePatchFromJson (library.load (fileName));
    REQUIRE (parsed.success);
    CHECK (parsed.document.meta.name == "With a picture");
    REQUIRE (parsed.document.assets.count ("img:1") == 1);
    juce::MemoryBlock decoded;
    REQUIRE (decodeBase64 (parsed.document.assets.at ("img:1").data, decoded));
    CHECK (decoded == image);
    CHECK (library.list().size() == 1);
    CHECK (library.list()[0].name == "With a picture");
}

TEST_CASE ("A plain-JSON patch still reads; garbage doesn't", "[plugin][patch][R0]")
{
    const auto json = bazalt::engine::serializePatchToJson (patchWithImage (someBytes (10)), true);
    juce::MemoryBlock bytes (json.toRawUTF8(), json.getNumBytesAsUTF8());
    juce::String out, error;
    REQUIRE (PatchFile::read (bytes, out, error));
    CHECK (bazalt::engine::parsePatchFromJson (out).success);

    juce::MemoryBlock garbage ("not a patch at all", 18);
    CHECK_FALSE (PatchFile::read (garbage, out, error));
    CHECK (error.isNotEmpty());
}

TEST_CASE ("The DAW state is gzip-compressed and round-trips; plain JSON state still loads", "[plugin][patch][R0]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    REQUIRE (processor.getGraphEditController().addNode ("source.sine", "sine", 0, 0).success);
    REQUIRE (processor.getGraphEditController().connect ("sine", "out", "masterOut", "in").success);

    juce::MemoryBlock state;
    processor.getStateInformation (state);
    REQUIRE (state.getSize() > 2);
    CHECK ((unsigned char) state[0] == 0x1f);
    CHECK ((unsigned char) state[1] == 0x8b);
    CHECK (state.getSize() < (size_t) processor.getStateAsJson().getNumBytesAsUTF8());

    BazaltAudioProcessor reloaded;
    reloaded.prepareToPlay (48000.0, 512);
    reloaded.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (reloaded.getGraphEditController().getGraph().findNode ("sine") != nullptr);

    BazaltAudioProcessor fromPlain;
    fromPlain.prepareToPlay (48000.0, 512);
    const auto json = processor.getStateAsJson();
    fromPlain.setStateInformation (json.toRawUTF8(), (int) json.getNumBytesAsUTF8());
    CHECK (fromPlain.getGraphEditController().getGraph().findNode ("sine") != nullptr);
}
