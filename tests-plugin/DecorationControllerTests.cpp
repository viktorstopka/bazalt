// wiki/plans/Decorations.md: images go in through GraphEditController::addImage
// (one undo step, stored once by content, refused past the patch limit);
// editing a decoration's text never recompiles.
#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"

using bazalt::BazaltAudioProcessor;
using bazalt::GraphEditController;

TEST_CASE ("addImage stores the image once and refuses past the patch limit", "[plugin][decorations]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);
    auto& controller = processor.getGraphEditController();

    const auto image = juce::String::repeatedString ("AAAA", 300 * 1024); // 900 KB decoded
    REQUIRE (controller.addImage ("img1", 0, 0, "image/webp", image, 200, 100).success);
    REQUIRE (controller.addImage ("img2", 300, 0, "image/webp", image, 200, 100).success); // same bytes: free
    CHECK (bazalt::engine::PatchDocument::fromNodeGraph (controller.getGraph()).assets.size() == 1);

    const auto big = juce::String::repeatedString ("BBBB", 1500 * 1024); // 4.4 MB more -> over 5 MB
    const auto refused = controller.addImage ("img3", 0, 300, "image/webp", big, 200, 100);
    CHECK_FALSE (refused.success);
    CHECK (refused.errorMessage.contains ("refused"));
    CHECK (controller.getGraph().findNode ("img3") == nullptr);

    CHECK_FALSE (controller.addImage ("img4", 0, 0, "text/html", "PHNjcmlwdD4=", 10, 10).success);
}

TEST_CASE ("Editing a decoration's text doesn't recompile", "[plugin][decorations]")
{
    BazaltAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);
    auto& controller = processor.getGraphEditController();
    REQUIRE (controller.addNode ("deco.comment", "note", 0, 0).success);

    const auto* planBefore = processor.getGlobalPlanSwapper().peekCurrentPlan();
    REQUIRE (controller.setProperty ("note", "text", "Hello").success);
    CHECK (processor.getGlobalPlanSwapper().peekCurrentPlan() == planBefore);
    CHECK (controller.getGraph().findNode ("note")->properties.at ("text").toString() == "Hello");
}
