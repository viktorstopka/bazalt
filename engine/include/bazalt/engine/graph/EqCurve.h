#pragma once

#include "bazalt/engine/patch/ExactFloat.h"
#include "bazalt/engine/Biquad.h"
#include "bazalt/engine/SvfCore.h"
#include "bazalt/engine/graph/Data.h"
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace bazalt::engine
{
    /** An EQ curve (wiki/plans/DataAndWavetable.md 1d): the content of
        `data.eqCurve` and `filter.eq` — a list of bands, each a type,
        frequency, gain (dB) and width.

        - **Bell** — a peak/dip around the frequency, width Q (`filter.peak`).
        - **Low / High Shelf** — everything below/above the frequency lifted or
          cut, `slope` 0..1 (`filter.shelf`).
        - **Low / High Cut** — a high-/low-pass, `order` 1..4 = 12..48 dB per
          octave as a Butterworth cascade of SVF stages; Q above 0.707 adds a
          resonant bump (`filter.svf`).
        - **Notch** — a narrow cut, width Q (`filter.svf`'s notch).

        A band becomes one or more **stages** (`stagesFor`), each exactly one
        primitive filter node with exactly the values that node would get —
        so an EQ and the Peak/Shelf/SVF chain it unwraps into compute the same
        samples (`tests/EqTests.cpp` checks it bit for bit). The engine
        publishes the stages, not the bands: the audio thread never computes
        a stage, it only plays them.
    */
    enum class EqBandType
    {
        Bell,
        LowShelf,
        HighShelf,
        LowCut,
        HighCut,
        Notch
    };

    struct EqBand
    {
        EqBandType type = EqBandType::Bell;
        float frequency = 1000.0f;
        float gainDb = 0.0f;
        float q = 1.0f;
        float slope = 0.5f; // shelves
        int order = 2;      // cuts: 1..4 (12..48 dB/oct)
        bool enabled = true;

        bool hasGain() const noexcept { return type == EqBandType::Bell || type == EqBandType::LowShelf || type == EqBandType::HighShelf; }
    };

    /** One primitive filter: what one Peak / Shelf / SVF node does. */
    struct EqStage
    {
        enum class Kind
        {
            Peak,
            LowShelf,
            HighShelf,
            SvfLowpass,
            SvfHighpass,
            SvfNotch
        };

        Kind kind = Kind::Peak;
        float frequency = 1000.0f;
        float gain = 1.0f;  // linear (Peak / Shelf)
        float q = 1.0f;     // Peak's Q, SVF's resonance
        float slope = 0.5f; // Shelf

        bool isSvf() const noexcept { return kind == Kind::SvfLowpass || kind == Kind::SvfHighpass || kind == Kind::SvfNotch; }
    };

    struct EqStages
    {
        static constexpr int maxPerBand = 4;
        std::array<EqStage, maxPerBand> stages {};
        int count = 0;
    };

    struct EqCurveDocument
    {
        static constexpr int maxBands = 16;
        static constexpr float minFrequency = 10.0f, maxFrequency = 22000.0f;
        static constexpr float maxGainDb = 24.0f;

        std::vector<EqBand> bands;

        static float dbToLinear (float db) noexcept { return (float) std::pow (10.0, (double) db / 20.0); }

        /** Q of section `index` of an order-`order` Butterworth cascade
            (2·order poles), scaled so the band's own Q stays the resonance
            control: one section at Q 0.707 is plain Butterworth. */
        static float cascadeQ (int order, int index, float bandQ) noexcept
        {
            const auto butterworth = 1.0 / (2.0 * std::cos (juce::MathConstants<double>::pi * (2.0 * index + 1.0) / (4.0 * order)));
            return (float) (butterworth * (double) bandQ / 0.70710678118654752);
        }

        static EqStages stagesFor (const EqBand& band) noexcept
        {
            EqStages out;
            if (! band.enabled)
                return out;
            const auto add = [&out] (EqStage stage) { out.stages[(size_t) out.count++] = stage; };
            switch (band.type)
            {
                case EqBandType::Bell:
                    add ({ EqStage::Kind::Peak, band.frequency, dbToLinear (band.gainDb), band.q, 0.5f });
                    break;
                case EqBandType::LowShelf:
                case EqBandType::HighShelf:
                    add ({ band.type == EqBandType::LowShelf ? EqStage::Kind::LowShelf : EqStage::Kind::HighShelf,
                           band.frequency, dbToLinear (band.gainDb), 1.0f, band.slope });
                    break;
                case EqBandType::LowCut:
                case EqBandType::HighCut:
                    for (int i = 0; i < band.order; ++i)
                        add ({ band.type == EqBandType::LowCut ? EqStage::Kind::SvfHighpass : EqStage::Kind::SvfLowpass,
                               band.frequency, 1.0f, cascadeQ (band.order, i, band.q), 0.5f });
                    break;
                case EqBandType::Notch:
                    add ({ EqStage::Kind::SvfNotch, band.frequency, 1.0f, band.q, 0.5f });
                    break;
            }
            return out;
        }

        // ---- JSON ----------------------------------------------------------

        static const char* typeName (EqBandType type) noexcept
        {
            switch (type)
            {
                case EqBandType::Bell:      return "bell";
                case EqBandType::LowShelf:  return "lowShelf";
                case EqBandType::HighShelf: return "highShelf";
                case EqBandType::LowCut:    return "lowCut";
                case EqBandType::HighCut:   return "highCut";
                case EqBandType::Notch:     return "notch";
            }
            return "bell";
        }

        static EqBandType typeFromName (const juce::String& name) noexcept
        {
            for (auto type : { EqBandType::Bell, EqBandType::LowShelf, EqBandType::HighShelf, EqBandType::LowCut,
                               EqBandType::HighCut, EqBandType::Notch })
                if (name == typeName (type))
                    return type;
            return EqBandType::Bell;
        }

        /** No content is a flat EQ: no bands. */
        static EqCurveDocument fromVar (const juce::var& content)
        {
            EqCurveDocument doc;
            if (auto* array = content.getProperty ("bands", {}).getArray())
            {
                for (const auto& b : *array)
                {
                    if ((int) doc.bands.size() >= maxBands)
                        break;
                    EqBand band;
                    band.type = typeFromName (b.getProperty ("type", "bell").toString());
                    band.frequency = (float) (double) b.getProperty ("frequency", 1000.0);
                    band.gainDb = (float) (double) b.getProperty ("gain", 0.0);
                    band.q = (float) (double) b.getProperty ("q", band.type == EqBandType::Bell || band.type == EqBandType::Notch ? 1.0 : 0.70710678);
                    band.slope = (float) (double) b.getProperty ("slope", 0.5);
                    band.order = (int) b.getProperty ("order", 2);
                    band.enabled = (bool) b.getProperty ("enabled", true);
                    if (! std::isfinite (band.frequency) || ! std::isfinite (band.gainDb) || ! std::isfinite (band.q))
                        continue;
                    band.frequency = juce::jlimit (minFrequency, maxFrequency, band.frequency);
                    band.gainDb = juce::jlimit (-maxGainDb, maxGainDb, band.gainDb);
                    band.q = juce::jlimit (0.1f, 30.0f, band.q);
                    band.slope = juce::jlimit (0.0f, 1.0f, std::isfinite (band.slope) ? band.slope : 0.5f);
                    band.order = juce::jlimit (1, EqStages::maxPerBand, band.order);
                    doc.bands.push_back (band);
                }
            }
            return doc;
        }

        juce::var toVar() const
        {
            auto* root = new juce::DynamicObject();
            root->setProperty ("schema", "eq");
            root->setProperty ("version", 1);
            juce::Array<juce::var> array;
            for (const auto& band : bands)
            {
                auto* b = new juce::DynamicObject();
                b->setProperty ("type", typeName (band.type));
                b->setProperty ("frequency", exactFloatVar (band.frequency));
                if (band.hasGain())
                    b->setProperty ("gain", exactFloatVar (band.gainDb));
                if (band.type == EqBandType::LowShelf || band.type == EqBandType::HighShelf)
                    b->setProperty ("slope", exactFloatVar (band.slope));
                else
                    b->setProperty ("q", exactFloatVar (band.q));
                if (band.type == EqBandType::LowCut || band.type == EqBandType::HighCut)
                    b->setProperty ("order", band.order);
                if (! band.enabled)
                    b->setProperty ("enabled", false);
                array.add (juce::var (b));
            }
            root->setProperty ("bands", array);
            return juce::var (root);
        }
    };

    /** The stages of every band, in order, as one immutable `Data(eq)`
        buffer: five floats per stage (kind, frequency, gain, q, slope). */
    inline std::unique_ptr<DataBuffer> buildEqBuffer (const EqCurveDocument& doc)
    {
        std::vector<float> values;
        values.reserve ((size_t) EqCurveDocument::maxBands * EqStages::maxPerBand * 5);
        for (const auto& band : doc.bands)
        {
            const auto stages = EqCurveDocument::stagesFor (band);
            for (int i = 0; i < stages.count; ++i)
            {
                const auto& stage = stages.stages[(size_t) i];
                values.insert (values.end(), { (float) stage.kind, stage.frequency, stage.gain, stage.q, stage.slope });
            }
        }
        return std::make_unique<DataBuffer> (DataTag::EqCurve, std::move (values), 5);
    }

    /** Plays a `Data(eq)` buffer on the audio thread: one fixed slot per
        stage (no allocation, ever), each a Biquad or an SVF exactly as the
        node it stands for runs it. A new buffer glides to its values over
        ~10 ms instead of jumping (no zipper while a band is dragged); a
        stage whose kind changes starts from silence. The first buffer is
        taken as is, so a static EQ computes exactly what its unwrapped
        chain does. */
    class EqProcessor
    {
    public:
        static constexpr int maxStages = EqCurveDocument::maxBands * EqStages::maxPerBand;

        void prepare (double sampleRateIn) noexcept
        {
            sampleRate = sampleRateIn;
            glideCoeff = (float) std::exp (-1.0 / (0.010 * sampleRate));
            for (auto& slot : slots)
            {
                slot.cachedValid = false;
                slot.svf.invalidate();
            }
            reset();
        }

        void reset() noexcept
        {
            for (auto& slot : slots)
            {
                slot.biquad.reset();
                slot.svf.reset();
            }
        }

        /** Takes a new buffer (or the same one again, cheaply). */
        void setBuffer (const DataBuffer* buffer) noexcept
        {
            // The generation too: a new buffer may reuse a freed one's address.
            if (buffer == current && (buffer == nullptr || buffer->generation == currentGeneration))
                return;
            const auto first = current == nullptr;
            current = buffer;
            currentGeneration = buffer != nullptr ? buffer->generation : 0;
            numStages = buffer != nullptr && buffer->tag() == DataTag::EqCurve ? juce::jmin (maxStages, buffer->length()) : 0;
            for (int i = 0; i < numStages; ++i)
            {
                auto& slot = slots[(size_t) i];
                EqStage target { (EqStage::Kind) juce::jlimit (0, 5, (int) buffer->at (i, 0)), buffer->at (i, 1), buffer->at (i, 2),
                                 buffer->at (i, 3), buffer->at (i, 4) };
                const auto kindChanged = ! slot.active || target.kind != slot.target.kind;
                slot.target = target;
                if (first || kindChanged)
                {
                    if (kindChanged && ! first)
                    {
                        slot.biquad.reset();
                        slot.svf.reset();
                    }
                    slot.live = target;
                    slot.gliding = false;
                }
                else
                {
                    slot.gliding = true;
                }
                slot.active = true;
            }
            for (int i = numStages; i < maxStages; ++i)
                slots[(size_t) i].active = false;
        }

        float processSample (float x) noexcept
        {
            for (int i = 0; i < numStages; ++i)
            {
                auto& slot = slots[(size_t) i];
                if (slot.gliding)
                    glide (slot);
                x = run (slot, x);
            }
            return x;
        }

    private:
        struct Slot
        {
            EqStage target, live;
            bool active = false, gliding = false;
            Biquad biquad;
            SvfCore svf;
            EqStage cached;
            bool cachedValid = false;
        };

        float run (Slot& slot, float x) noexcept
        {
            const auto& s = slot.live;
            if (s.isSvf())
            {
                slot.svf.setCoefficients (sampleRate, s.frequency, s.q);
                const auto [low, band, high] = slot.svf.process (x);
                juce::ignoreUnused (band);
                return s.kind == EqStage::Kind::SvfLowpass ? (float) low
                     : s.kind == EqStage::Kind::SvfHighpass ? (float) high
                                                             : (float) (low + high);
            }

            if (! slot.cachedValid || s.kind != slot.cached.kind || s.frequency != slot.cached.frequency || s.gain != slot.cached.gain
                || s.q != slot.cached.q || s.slope != slot.cached.slope)
            {
                slot.cached = s;
                slot.cachedValid = true;
                if (s.kind == EqStage::Kind::Peak)
                    slot.biquad.setPeak (sampleRate, s.frequency, s.q, s.gain);
                else
                    slot.biquad.setShelf (sampleRate, s.frequency, s.gain, s.slope, s.kind == EqStage::Kind::HighShelf);
            }
            return slot.biquad.processSample (x);
        }

        /** Frequency, gain and Q glide geometrically (as heard); within a
            hair of the target they land on it exactly. */
        void glide (Slot& slot) noexcept
        {
            auto step = [this] (float from, float to)
            {
                if (from <= 0.0f || to <= 0.0f)
                    return to;
                const auto next = to * std::pow (from / to, glideCoeff);
                return std::abs (next / to - 1.0f) < 1.0e-4f ? to : next;
            };
            slot.live.frequency = step (slot.live.frequency, slot.target.frequency);
            slot.live.gain = step (slot.live.gain, slot.target.gain);
            slot.live.q = step (slot.live.q, slot.target.q);
            slot.live.slope = slot.target.slope + (slot.live.slope - slot.target.slope) * glideCoeff;
            if (std::abs (slot.live.slope - slot.target.slope) < 1.0e-4f)
                slot.live.slope = slot.target.slope;
            if (slot.live.frequency == slot.target.frequency && slot.live.gain == slot.target.gain && slot.live.q == slot.target.q
                && slot.live.slope == slot.target.slope)
                slot.gliding = false;
        }

        double sampleRate = 48000.0;
        float glideCoeff = 0.99f;
        std::array<Slot, maxStages> slots {};
        const DataBuffer* current = nullptr;
        uint64_t currentGeneration = 0;
        int numStages = 0;
    };
}
