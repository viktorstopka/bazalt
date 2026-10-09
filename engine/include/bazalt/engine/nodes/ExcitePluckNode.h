#pragma once

#include "bazalt/engine/graph/Node.h"
#include <cmath>
#include <vector>

namespace bazalt::engine::nodes
{
    /** Stable type id: "excite.pluck" (wiki/NODES.md's `excite.*` row, the
        PM Core batch). "A pre-shaped plucked-string excitation, pickup
        position and hardness baked in — the one-node shortcut into
        `resonator.string` without hand-building the comb-notch spectrum
        yourself" (the catalog's own framing). Unlike `resonator.string`'s
        own `position` (a difference-tap read directly off the string's real
        delay line, so it's relative to the string's actual sounding pitch),
        this node has no pitch concept at all — it only shapes a short,
        pitch-independent excitation transient, meant to be fed into
        `resonator.string`'s `excite` input (or any other resonator).

        **A concrete, tested contract this session had to design** (the
        catalog names `position`/`hardness`, not their exact mechanism):
        - A fixed, short (5ms) linearly-decaying noise burst — the same
          envelope shape `excite.burst` already uses, just much shorter
          (a pluck is a transient, not a sustained burst).
        - `hardness` reuses `filter.onepole`'s own "Damping" convention
          exactly (`0` = darkest/softest, `1` = brightest/hardest) as an
          in-line one-pole on the noise itself — a harder pluck (fingernail,
          pick) is brighter; a softer one (fingertip, felt) is duller.
        - `position` applies a plain FIR difference-tap
          (`y[n] = x[n] - 0.5·x[n - offset]`) against a small, FIXED
          200-sample reference buffer — a real comb-notch, the catalog's own
          named mechanism, but deliberately NOT scaled to any particular
          absolute pitch (this node doesn't know what pitch it'll end up
          exciting): a fixed reference window still produces the
          characteristic "harder/softer attack" comb coloration a pickup
          position gives, it just isn't tuned to null out a SPECIFIC
          harmonic the way `resonator.string`'s own `position` tap is
          (which DOES know the real delay length).

        **`amplitude`/`hardness`/`position` are all sampled once, at the
        moment `trigger` fires** — not continuously tracked during the
        decay. A real pluck has one fixed attack character; nothing about a
        real plucked-string excitation changes mid-ring after the initial
        strike, so there is no "live modulation" case being given up here,
        and it keeps a resonator fed by this node sounding like one coherent
        pluck rather than a filter sweeping underneath it. Same
        once-at-trigger convention `excite.burst`'s own `duration` already
        establishes.
    */
    class ExcitePluckNode : public Node
    {
    public:
        static constexpr float burstDurationMs = 5.0f;
        static constexpr int combBufferLength = 200;
        static constexpr int numInputs = 4;  // trigger, position, hardness, amplitude
        static constexpr int numOutputs = 1;

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            totalSamples = juce::jmax (1, (int) std::lround ((double) burstDurationMs * 0.001 * sampleRate));
            combBuffer.assign ((size_t) combBufferLength, 0.0f);
        }

        void reset() override
        {
            std::fill (combBuffer.begin(), combBuffer.end(), 0.0f);
            combWriteIndex = 0;
            dampingState = 0.0f;
            remainingSamples = 0;
        }

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Pluck"; }
        juce::String getCategory() const override { return "Excite"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "trigger", .type = SignalType::Event, .label = "Trigger" },
                unipolarPort ("excite.pluck.position", "Position", 0.3f),
                unipolarPort ("excite.pluck.hardness", "Hardness", 0.5f),
                unipolarPort ("excite.pluck.amplitude", "Amplitude", 1.0f),
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { { .id = "out", .type = SignalType::Signal, .quantity = Quantity::Audio } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "excite.pluck.position")
                storedPosition = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "excite.pluck.hardness")
                storedHardness = juce::jlimit (0.0f, 1.0f, value);
            else if (parameterId == "excite.pluck.amplitude")
                storedAmplitude = juce::jlimit (0.0f, 1.0f, value);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            if (std::fabs (inputs[0]) > 0.0f)
            {
                firedPosition = std::isnan (inputs[1]) ? storedPosition : juce::jlimit (0.0f, 1.0f, inputs[1]);
                firedHardness = std::isnan (inputs[2]) ? storedHardness : juce::jlimit (0.0f, 1.0f, inputs[2]);
                firedAmplitude = std::isnan (inputs[3]) ? storedAmplitude : juce::jlimit (0.0f, 1.0f, inputs[3]);
                remainingSamples = totalSamples;
            }

            if (remainingSamples <= 0)
            {
                outputs[0] = 0.0f;
                return;
            }

            const auto envelope = (float) remainingSamples / (float) totalSamples;
            const auto noiseSample = (random.nextFloat() * 2.0f - 1.0f) * envelope * firedAmplitude;

            // hardness-controlled brightness (filter.onepole's own Damping convention).
            dampingState = firedHardness * noiseSample + (1.0f - firedHardness) * dampingState;

            // position comb-notch against the fixed reference buffer.
            const auto offset = juce::jlimit (1, combBufferLength - 1, (int) std::lround (firedPosition * (float) (combBufferLength - 1)));
            auto readIndex = combWriteIndex - offset;
            if (readIndex < 0)
                readIndex += combBufferLength;
            const auto tapped = combBuffer[(size_t) readIndex];

            outputs[0] = dampingState - 0.5f * tapped;

            combBuffer[(size_t) combWriteIndex] = dampingState;
            combWriteIndex = (combWriteIndex + 1) % combBufferLength;

            --remainingSamples;
        }

    private:
        static PortDescriptor unipolarPort (juce::String id, juce::String label, float defaultValue)
        {
            return PortDescriptor { .id = std::move (id), .type = SignalType::Signal, .label = std::move (label),
                                     .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = defaultValue,
                                     .hasFallbackWhenUnconnected = true, .quantity = Quantity::Unipolar,
                                     .polarity = Polarity::Unipolar };
        }

        juce::Random random;
        double sampleRate = 44100.0;
        int totalSamples = 1;
        int remainingSamples = 0;

        std::vector<float> combBuffer;
        int combWriteIndex = 0;
        float dampingState = 0.0f;

        float storedPosition = 0.3f, storedHardness = 0.5f, storedAmplitude = 1.0f;
        float firedPosition = 0.3f, firedHardness = 0.5f, firedAmplitude = 1.0f;
    };
}
