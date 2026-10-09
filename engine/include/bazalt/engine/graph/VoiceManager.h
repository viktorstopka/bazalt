#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace bazalt::engine
{
    enum class VoiceStage
    {
        Idle,
        Active,
        Releasing,

        /** M17 (DOMAINS.md §5: "a stolen instance is faded out over a
            short ramp rather than cut"). Entered instead of jumping
            straight to Active when noteOn() has to steal a busy voice —
            the voice keeps rendering its OLD (pre-steal) content, ramped
            down over `VoiceManager::stealFadeSamples`, before the new
            note's actual onset happens (`completeSteal()`). The new
            note's data is held in `pendingNoteOn` until then.
        */
        Stealing
    };

    /** Fixed-size voice pool (ARCHITECTURE.md §3.5) — sized once at
        prepare(), no runtime allocation thereafter. Stealing policy for
        MVP: steal the oldest voice in its release stage, falling back to
        the oldest voice overall. Each voice carries a NoteID (not just a
        MIDI channel/number pair), and lookups are keyed by NoteID rather
        than voice index, so a future unison mode can map one triggered
        note to N render voices without changing this model — MVP always
        maps 1:1.

        M17 additions: a generic per-voice silence accumulator (replacing
        a hardcoded "check this one specific node's isActive()" — DOMAINS.md
        §5's actual-signal-level model, RECONCILIATION.md 3.2) and the
        fade-on-steal state machine above. Both are pure bookkeeping here;
        PluginProcessor does the actual level measurement and rendering.
    */
    class VoiceManager
    {
    public:
        using NoteId = uint32_t;

        /** DOMAINS.md §5 doesn't mandate a specific ramp length — 256
            samples (~5.8ms @ 44.1kHz) is short enough to be inaudible as a
            delay to a stolen note's onset, long enough that the per-sample
            linear ramp PluginProcessor applies over it has no audible
            stepping.
        */
        static constexpr int stealFadeSamples = 256;

        struct PendingNoteOn
        {
            NoteId noteId = 0;
            float pitch = 60.0f; // absolute MIDI note number (M18, ADR-0024) — was "frequency" (Hz) before pitch became a real port
            float velocity = 1.0f;
        };

        void prepare (int numVoices)
        {
            voices.assign ((size_t) numVoices, Voice {});
            nextAge = 0;
            maxActiveVoices.store (numVoices, std::memory_order_relaxed);
        }

        /** wiki/plans/DomainRedesign.md Batch 4: "life.voice.
            maxInstances" — declared since M17, read nowhere until now
            (confirmed against the real source: this pool was always
            hardcoded to `numVoices`, independent of it). Lanes at or past
            this ceiling are simply never allocated or stolen — the
            live/compiled ceiling the UI's instance-count badge shows
            ("2/8") is exactly this value. Clamped to [1, the real pool
            size] — a graph asking for more than this VoiceManager actually
            has can't be granted more than physically exist; the ceiling
            can only ever narrow the real pool, never grow it. Message-
            thread only to call (GraphEditController, after compiling);
            findIdleVoice()/stealVoice() read it on the audio thread, hence
            atomic.
        */
        void setMaxActiveVoices (int count) noexcept
        {
            const auto clamped = std::max (1, std::min ((int) voices.size(), count));
            maxActiveVoices.store (clamped, std::memory_order_relaxed);
        }

        int getMaxActiveVoices() const noexcept { return maxActiveVoices.load (std::memory_order_relaxed); }

        /** How many lanes are currently NOT Idle — the live half of the
            instance-count badge ("2/8": this value over getMaxActiveVoices()).
            Recomputed and published (recomputeActiveVoiceCount()) on the
            audio thread every time a lane's stage actually changes; read
            from the message thread (GraphEditController, servicing a UI
            poll) via this atomic.
        */
        int getActiveVoiceCount() const noexcept { return activeVoiceCount.load (std::memory_order_relaxed); }

        /** Allocates a voice for noteId (stealing if necessary) and returns
            its index. If stealing was required, the returned voice enters
            `Stealing` (not `Active`) — caller must store the actual note
            data via `setPendingNoteOn()` and must NOT touch that voice's
            plan yet; `Active` only begins once `completeSteal()` runs
            (PluginProcessor::renderVoiceRange, once the fade finishes).
        */
        int noteOn (NoteId noteId) noexcept
        {
            auto index = findIdleVoice();
            if (index != -1)
            {
                auto& voice = voices[(size_t) index];
                voice.stage = VoiceStage::Active;
                voice.noteId = noteId;
                voice.age = nextAge++;
                voice.silentSamplesAccumulated = 0;
                mostRecentlyTriggeredVoice.store (index, std::memory_order_relaxed);
                recomputeActiveVoiceCount();
                return index;
            }

            index = stealVoice();
            auto& voice = voices[(size_t) index];
            voice.stage = VoiceStage::Stealing;
            voice.stealFadeSamplesRemaining = stealFadeSamples;
            voice.stealFadeGainAtStart = 1.0f;
            mostRecentlyTriggeredVoice.store (index, std::memory_order_relaxed);
            recomputeActiveVoiceCount(); // a stolen voice was already non-Idle; the count itself doesn't change, but stays correct either way
            return index;
        }

        /** M20 — which voice most recently received a noteOn() call
            (whether it went straight to Active or is still mid-steal-fade).
            Used to decide which of this voice's 8 independent
            ExecutionPlans a voice-domain preview tap should currently read
            from: the one you're most likely actually looking at right
            after playing a note (this plan's own explicit design choice,
            over always-voice-0 or an 8-voice aggregate). -1 before the
            first note is ever triggered.
        */
        int getMostRecentlyTriggeredVoice() const noexcept { return mostRecentlyTriggeredVoice.load (std::memory_order_relaxed); }

        void setPendingNoteOn (int voiceIndex, PendingNoteOn pending) noexcept
        {
            voices[(size_t) voiceIndex].pendingNoteOn = pending;
        }

        const PendingNoteOn& getPendingNoteOn (int voiceIndex) const noexcept
        {
            return voices[(size_t) voiceIndex].pendingNoteOn;
        }

        /** Finalizes a steal once its fade has fully run out — the voice
            becomes Active under the new note, exactly as if it had been
            an ordinary idle-voice allocation.
        */
        void completeSteal (int voiceIndex) noexcept
        {
            auto& voice = voices[(size_t) voiceIndex];
            voice.stage = VoiceStage::Active;
            voice.noteId = voice.pendingNoteOn.noteId;
            voice.age = nextAge++;
            voice.silentSamplesAccumulated = 0;
            recomputeActiveVoiceCount(); // Stealing -> Active either way; kept for symmetry with every other stage change
        }

        /** Consumes up to `numSamples` of remaining fade time, returning
            the gain to ramp FROM (the gain at the start of this call —
            caller linearly interpolates from this down to whatever
            `getStealFadeGain()` returns after this call, over
            `min(numSamples, samples actually still fading)`). Returns
            true once the fade is fully spent — caller renders any
            remaining samples in this range as silence for the old voice,
            then calls completeSteal() and proceeds with the new note for
            the rest of the range.
        */
        bool advanceStealFade (int voiceIndex, int numSamples) noexcept
        {
            auto& voice = voices[(size_t) voiceIndex];
            voice.stealFadeGainAtStart = getStealFadeGain (voiceIndex);
            voice.stealFadeSamplesRemaining -= numSamples;

            if (voice.stealFadeSamplesRemaining <= 0)
            {
                voice.stealFadeSamplesRemaining = 0;
                return true;
            }

            return false;
        }

        float getStealFadeGainAtStart (int voiceIndex) const noexcept { return voices[(size_t) voiceIndex].stealFadeGainAtStart; }
        int getStealFadeSamplesRemaining (int voiceIndex) const noexcept { return voices[(size_t) voiceIndex].stealFadeSamplesRemaining; }

        float getStealFadeGain (int voiceIndex) const noexcept
        {
            const auto& voice = voices[(size_t) voiceIndex];
            return (float) voice.stealFadeSamplesRemaining / (float) stealFadeSamples;
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
                    return i; // still non-Idle (Releasing) — the active count doesn't change
                }
            }

            return -1;
        }

        /** Called once a released voice's tail has actually finished —
            frees it back to Idle.
        */
        void voiceFinished (int voiceIndex) noexcept
        {
            voices[(size_t) voiceIndex].stage = VoiceStage::Idle;
            recomputeActiveVoiceCount();
        }

        /** Generic silence detector (DOMAINS.md §5, RECONCILIATION.md 3.2)
            — replaces a hardcoded "dynamic_cast a specific node and check
            its own isActive() flag" with a measurement of the ACTUAL
            signal reaching the domain boundary, which correctly lets a
            per-voice delay/reverb tail ring out past its envelope's own
            release. `peakLevel` is this render sub-range's peak absolute
            sample value; `thresholdLinear`/`holdTimeSamples` come from
            whatever policy the caller applies (PluginProcessor uses fixed
            defaults for M17 — see its own comment on why per-graph
            configurability via a real instance.sum node's parameters is a
            later integration, not required for this mechanism to be
            correct and generic). Returns true once the hold time has been
            exceeded — caller should then call voiceFinished().
        */
        bool updateSilenceAndCheckFinished (int voiceIndex, float peakLevel, int numSamples,
                                             float thresholdLinear, int holdTimeSamples) noexcept
        {
            auto& voice = voices[(size_t) voiceIndex];

            if (peakLevel < thresholdLinear)
                voice.silentSamplesAccumulated += numSamples;
            else
                voice.silentSamplesAccumulated = 0;

            return voice.silentSamplesAccumulated >= holdTimeSamples;
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
            int silentSamplesAccumulated = 0;

            PendingNoteOn pendingNoteOn;
            int stealFadeSamplesRemaining = 0;
            float stealFadeGainAtStart = 1.0f;
        };

        int findIdleVoice() const noexcept
        {
            const auto ceiling = std::min ((int) voices.size(), maxActiveVoices.load (std::memory_order_relaxed));
            for (int i = 0; i < ceiling; ++i)
                if (voices[(size_t) i].stage == VoiceStage::Idle)
                    return i;
            return -1;
        }

        int stealVoice() const noexcept
        {
            const auto ceiling = std::min ((int) voices.size(), maxActiveVoices.load (std::memory_order_relaxed));
            int oldestReleasing = -1;
            uint64_t oldestReleasingAge = 0;
            int oldestOverall = -1;
            uint64_t oldestOverallAge = 0;

            for (int i = 0; i < ceiling; ++i)
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

        // wiki/plans/DomainRedesign.md Batch 4: recomputed on every stage
        // change (noteOn/noteOff/completeSteal/voiceFinished) rather than
        // counted lazily on read — this is a plain linear scan over an
        // 8-ish-element vector, cheap enough to redo on every transition,
        // and means getActiveVoiceCount() is a single atomic load with no
        // audio-thread work at all on the READING (message-thread) side.
        void recomputeActiveVoiceCount() noexcept
        {
            int count = 0;
            for (const auto& voice : voices)
                if (voice.stage != VoiceStage::Idle)
                    ++count;
            activeVoiceCount.store (count, std::memory_order_relaxed);
        }

        std::vector<Voice> voices;
        uint64_t nextAge = 0;
        // Written by the audio thread (noteOn), read by the message thread when
        // it attaches preview taps (ADR-0029) - atomic so that is not a data race.
        std::atomic<int> mostRecentlyTriggeredVoice { -1 };
        // Both written from the message thread (setMaxActiveVoices(), a real
        // recompile) and read from the audio thread (findIdleVoice()/
        // stealVoice(), every note-on) - or the reverse, for
        // getActiveVoiceCount() - atomic for the same reason
        // mostRecentlyTriggeredVoice already is.
        std::atomic<int> maxActiveVoices { 8 };
        std::atomic<int> activeVoiceCount { 0 };
    };
}
