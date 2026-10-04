// UserPatchLibrary: the user's saved patches in the app-data folder, tested
// against a temporary directory.
#include <catch2/catch_test_macros.hpp>
#include "UserPatchLibrary.h"
#include "bazalt/engine/graph/ProofGraphs.h"

using namespace bazalt;

TEST_CASE ("UserPatchLibrary saves, lists, overwrites, loads and deletes patches", "[plugin][patches]")
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("bazalt-patch-test-" + juce::Uuid().toString());
    UserPatchLibrary library (dir);
    const auto document = engine::PatchDocument::fromNodeGraph (engine::buildMasterOutOnlyGraph());

    CHECK (library.list().isEmpty()); // no directory yet is fine

    juce::String error;
    const auto fileName = library.save ("  My Bass  ", document, error);
    REQUIRE (fileName == "My Bass.bazalt");
    CHECK (error.isEmpty());
    CHECK (library.exists ("My Bass"));
    library.save ("a pad", document, error);

    const auto entries = library.list();
    REQUIRE (entries.size() == 2);
    CHECK (entries[0].name == "a pad"); // sorted case-insensitively
    CHECK (entries[1].name == "My Bass");

    // The saved file is an ordinary patch carrying its own name.
    const auto loaded = engine::parsePatchFromJson (library.load (fileName));
    REQUIRE (loaded.success);
    CHECK (loaded.document.meta.name == "My Bass");
    const auto created = loaded.document.meta.createdAtMs;

    // Overwriting keeps the original creation time.
    juce::Thread::sleep (5);
    library.save ("My Bass", document, error);
    CHECK (engine::parsePatchFromJson (library.load (fileName)).document.meta.createdAtMs == created);
    CHECK (library.list().size() == 2);

    // Only bare file names inside the library are accepted.
    CHECK (library.load ("../My Bass.bazalt").isEmpty());
    CHECK_FALSE (library.remove ("sub/My Bass.bazalt"));

    CHECK (library.remove (fileName));
    CHECK (library.list().size() == 1);
    CHECK (library.save ("   ", document, error).isEmpty());
    CHECK (error.isNotEmpty());

    dir.deleteRecursively();
}
