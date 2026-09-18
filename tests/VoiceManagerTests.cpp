#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/VoiceManager.h"

using namespace bazalt::engine;

TEST_CASE ("VoiceManager allocates distinct idle voices until the pool is exhausted", "[engine][VoiceManager]")
{
    VoiceManager voices;
    voices.prepare (4);

    const int v0 = voices.noteOn (100);
    const int v1 = voices.noteOn (101);
    const int v2 = voices.noteOn (102);
    const int v3 = voices.noteOn (103);

    CHECK (v0 != v1);
    CHECK (v0 != v2);
    CHECK (v0 != v3);
    CHECK (v1 != v2);
    CHECK (v1 != v3);
    CHECK (v2 != v3);

    for (int v : { v0, v1, v2, v3 })
        CHECK (voices.getStage (v) == VoiceStage::Active);
}

TEST_CASE ("VoiceManager steals the oldest RELEASING voice before touching any active voice", "[engine][VoiceManager]")
{
    VoiceManager voices;
    voices.prepare (2);

    const int v0 = voices.noteOn (1); // oldest
    const int v1 = voices.noteOn (2); // newest, still active

    voices.noteOff (1); // v0 -> Releasing

    const int stolen = voices.noteOn (3);

    CHECK (stolen == v0); // the releasing voice, not the active v1
    CHECK (voices.getStage (v1) == VoiceStage::Active);
    CHECK (voices.getNoteId (v1) == 2u);

    // M17: a steal doesn't become Active immediately — it enters Stealing
    // and fades its old content first (DOMAINS.md §5); the caller finishes
    // it via completeSteal() once the fade runs out (PluginProcessor does
    // this in renderVoiceRange, not here).
    CHECK (voices.getStage (stolen) == VoiceStage::Stealing);
    voices.setPendingNoteOn (stolen, { 3u, 440.0f, 1.0f });
    voices.completeSteal (stolen);
    CHECK (voices.getStage (stolen) == VoiceStage::Active);
    CHECK (voices.getNoteId (stolen) == 3u);
}

TEST_CASE ("VoiceManager falls back to the oldest voice overall when none are releasing", "[engine][VoiceManager]")
{
    VoiceManager voices;
    voices.prepare (2);

    const int v0 = voices.noteOn (1); // oldest, stays Active
    voices.noteOn (2);                // newest, stays Active

    const int stolen = voices.noteOn (3); // both active -> steal the oldest

    CHECK (stolen == v0);
    CHECK (voices.getStage (stolen) == VoiceStage::Stealing);
    voices.setPendingNoteOn (stolen, { 3u, 440.0f, 1.0f });
    voices.completeSteal (stolen);
    CHECK (voices.getNoteId (stolen) == 3u);
}

TEST_CASE ("VoiceManager fades a stolen voice's gain to zero over stealFadeSamples, never a sudden cut",
           "[engine][VoiceManager][M17]")
{
    VoiceManager voices;
    voices.prepare (1);

    voices.noteOn (1);
    const int stolen = voices.noteOn (2); // only voice -> steal itself
    REQUIRE (voices.getStage (stolen) == VoiceStage::Stealing);
    CHECK (voices.getStealFadeGain (stolen) == 1.0f);

    const auto half = VoiceManager::stealFadeSamples / 2;
    CHECK_FALSE (voices.advanceStealFade (stolen, half));
    CHECK (voices.getStealFadeGain (stolen) > 0.49f);
    CHECK (voices.getStealFadeGain (stolen) < 0.51f);

    CHECK (voices.advanceStealFade (stolen, VoiceManager::stealFadeSamples - half)); // completes
    CHECK (voices.getStealFadeGain (stolen) == 0.0f);
}

TEST_CASE ("VoiceManager::noteOff finds the voice by NoteId, not by index", "[engine][VoiceManager]")
{
    VoiceManager voices;
    voices.prepare (4);

    voices.noteOn (10);
    const int v1 = voices.noteOn (20);
    voices.noteOn (30);

    const int released = voices.noteOff (20);

    CHECK (released == v1);
    CHECK (voices.getStage (v1) == VoiceStage::Releasing);
    CHECK (voices.noteOff (999) == -1); // not playing -> no-op, reports -1
}

TEST_CASE ("VoiceManager::voiceFinished frees a voice back to Idle for reuse", "[engine][VoiceManager]")
{
    VoiceManager voices;
    voices.prepare (1);

    const int v0 = voices.noteOn (1);
    voices.noteOff (1);
    CHECK (voices.getStage (v0) == VoiceStage::Releasing);

    voices.voiceFinished (v0);
    CHECK (voices.getStage (v0) == VoiceStage::Idle);

    const int reused = voices.noteOn (2);
    CHECK (reused == v0);
}
