#pragma once

#include <cstdint>
#include <vector>

namespace bazalt::engine
{
    enum class VoiceStage
    {
        Idle,
        Active,
        Releasing
    };

    /** Fixed-size voice pool (ARCHITECTURE.md §3.5) — sized once at
        prepare(), no runtime allocation thereafter. Stealing policy for
        MVP: steal the oldest voice in its release stage, falling back to
        the oldest voice overall. Each voice carries a NoteID (not just a
        MIDI channel/number pair), and lookups are keyed by NoteID rather
        than voice index, so a future unison mode can map one triggered
        note to N render voices without changing this model — MVP always
        maps 1:1.
    */
    class VoiceManager
    {
    public:
        using NoteId = uint32_t;

        void prepare (int numVoices)
        {
            voices.assign ((size_t) numVoices, Voice {});
            nextAge = 0;
        }

        /** Allocates a voice for noteId (stealing if necessary) and returns
            its index.
        */
        int noteOn (NoteId noteId) noexcept
        {
            auto index = findIdleVoice();
            if (index == -1)
                index = stealVoice();

            auto& voice = voices[(size_t) index];
            voice.stage = VoiceStage::Active;
            voice.noteId = noteId;
            voice.age = nextAge++;

            return index;
        }

        /** Moves the voice currently playing noteId into the release
            stage and returns its index, or -1 if noteId isn't sounding.
            MVP maps NoteId -> at most one voice; the -1-or-index return
            (rather than a list) reflects that, ready to become a list once
            unison needs one voiceOn per NoteId to become several.
        */
        int noteOff (NoteId noteId) noexcept
        {
            for (int i = 0; i < (int) voices.size(); ++i)
            {
                auto& voice = voices[(size_t) i];
                if (voice.stage == VoiceStage::Active && voice.noteId == noteId)
                {
                    voice.stage = VoiceStage::Releasing;
                    return i;
                }
            }

            return -1;
        }

        /** Called once a released voice's tail has actually finished (e.g.
            its ADSR is no longer active) — frees it back to Idle.
        */
        void voiceFinished (int voiceIndex) noexcept
        {
            voices[(size_t) voiceIndex].stage = VoiceStage::Idle;
        }

        VoiceStage getStage (int voiceIndex) const noexcept { return voices[(size_t) voiceIndex].stage; }
        NoteId getNoteId (int voiceIndex) const noexcept { return voices[(size_t) voiceIndex].noteId; }
        int getNumVoices() const noexcept { return (int) voices.size(); }

    private:
        struct Voice
        {
            VoiceStage stage = VoiceStage::Idle;
            NoteId noteId = 0;
            uint64_t age = 0;
        };

        int findIdleVoice() const noexcept
        {
            for (int i = 0; i < (int) voices.size(); ++i)
                if (voices[(size_t) i].stage == VoiceStage::Idle)
                    return i;
            return -1;
        }

        int stealVoice() const noexcept
        {
            int oldestReleasing = -1;
            uint64_t oldestReleasingAge = 0;
            int oldestOverall = -1;
            uint64_t oldestOverallAge = 0;

            for (int i = 0; i < (int) voices.size(); ++i)
            {
                const auto& voice = voices[(size_t) i];

                if (oldestOverall == -1 || voice.age < oldestOverallAge)
                {
                    oldestOverall = i;
                    oldestOverallAge = voice.age;
                }

                if (voice.stage == VoiceStage::Releasing && (oldestReleasing == -1 || voice.age < oldestReleasingAge))
                {
                    oldestReleasing = i;
                    oldestReleasingAge = voice.age;
                }
            }

            return oldestReleasing != -1 ? oldestReleasing : oldestOverall;
        }

        std::vector<Voice> voices;
        uint64_t nextAge = 0;
    };
}
