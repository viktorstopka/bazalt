#pragma once

#include "bazalt/engine/graph/Node.h"
#include "bazalt/engine/graph/SampleData.h"
#include <array>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "source.sampler" — Sampler (wiki/plans/Sampling.md 3a.2).
        The one player for every use: under a Population it is polyphonic, with
        many short members it is granular, at Rate 0 it scrubs.

        - **Sample**: a `Data(sample)` (a Sample node, later Record / bakes).
        - **Start** (Event) plays from **Position** (0–1 of the sample). With
          Start unwired it plays when it starts (a member's birth, or patch
          load). **Stop** (Event) ends it.
        - **Rate** (1 = original speed; negative plays backwards) times
          **Pitch** against the sample's root (`root` from the Sample, else
          60): a keyboard plays the sample at its notes.
        - **Rate 0 scrubs**: the head sits at Position, so moving Position is
          heard directly (the granular read head).
        - **Slice** (wired): Start plays slice N, up to the next slice.
        - **Channel** (wired): which channel of a many-channel sample; a
          fractional value blends two neighbours (a baked lattice's axis).
          Unwired: channels 1 and 2 (mono plays on both).
        - **Gate** holds a sustain loop: with Loop = While held, the loop
          plays while the gate is up, then the rest of the sample.

        Interpolation: linear, cubic (Catmull-Rom) or sinc (8 taps, Lanczos).
        Played faster than its own rate the sample reads a band-limited copy
        (`SampleMeta::mipLevels`, built by the publisher), so octaves up don't
        alias. Block-size invariant: everything is per sample. */
    class SamplerNode : public Node
    {
    public:
        enum Input { startIn, stopIn, positionIn, rateIn, pitchIn, sliceIn, channelIn, gateIn, numInputs };
        enum Output { outLeft, outRight, gateOut, positionOut, endOut, numFlatOutputs };
        enum class Loop { Off, Forward, PingPong, WhileHeld };
        enum class Interpolation { Linear, Cubic, Sinc };

        void prepare (const NodePrepareInfo& info) override { outputRate = info.sampleRate > 0.0 ? info.sampleRate : 48000.0; }

        void reset() override
        {
            playing = autoStart;
            pendingStart = autoStart;
            head = 0.0;
            direction = 1.0;
            sliceEnd = -1.0;
            gateWasHigh = false;
        }

        int getNumInputPorts() const noexcept override { return numInputs + 1; } // + sample
        int getNumOutputPorts() const noexcept override { return 4; }
        juce::String getTitle() const override { return "Sampler"; }
        juce::String getCategory() const override { return "Sources"; }
        bool supportsPerSample() const noexcept override { return false; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                PortDescriptor { .id = "sample", .type = SignalType::Data, .label = "Sample", .dataTags = { DataTag::Sample } },
                PortDescriptor { .id = "start", .type = SignalType::Event, .label = "Start" },
                PortDescriptor { .id = "stop", .type = SignalType::Event, .label = "Stop" },
                PortDescriptor { .id = "source.sampler.position", .type = SignalType::Signal, .label = "Position", .minValue = 0.0f,
                                 .maxValue = 1.0f, .defaultValue = 0.0f, .hasFallbackWhenUnconnected = true,
                                 .quantity = Quantity::Unipolar, .polarity = Polarity::Unipolar },
                PortDescriptor { .id = "source.sampler.rate", .type = SignalType::Signal, .label = "Rate", .minValue = -4.0f,
                                 .maxValue = 4.0f, .defaultValue = 1.0f, .hasFallbackWhenUnconnected = true, .quantity = Quantity::Ratio },
                PortDescriptor { .id = "pitch", .type = SignalType::Signal, .label = "Pitch", .unit = "st", .minValue = 0.0f,
                                 .maxValue = 127.0f, .defaultValue = 60.0f, .hasFallbackWhenUnconnected = true, .quantity = Quantity::Pitch },
                PortDescriptor { .id = "slice", .type = SignalType::Signal, .label = "Slice", .isInteger = true,
                                 .hasFallbackWhenUnconnected = true, .quantity = Quantity::Count, .step = 1.0f },
                PortDescriptor { .id = "channel", .type = SignalType::Signal, .label = "Channel", .hasFallbackWhenUnconnected = true,
                                 .quantity = Quantity::Count },
                PortDescriptor { .id = "gate", .type = SignalType::Signal, .label = "Gate", .hasFallbackWhenUnconnected = true,
                                 .quantity = Quantity::Boolean },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "out", .type = SignalType::Signal, .label = "Out", .isPrimaryOutput = true, .quantity = Quantity::Audio,
                                 .channels = Channels::Stereo },
                PortDescriptor { .id = "playing", .type = SignalType::Signal, .label = "Playing", .quantity = Quantity::Boolean },
                PortDescriptor { .id = "position", .type = SignalType::Signal, .label = "Position", .quantity = Quantity::Unipolar,
                                 .polarity = Polarity::Unipolar },
                PortDescriptor { .id = "end", .type = SignalType::Event, .label = "End" },
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "source.sampler.loop", .minValue = 0.0f, .maxValue = 3.0f, .defaultValue = 0.0f,
                                      .displayName = "Loop", .isInteger = true, .kind = ValueKind::Enum,
                                      .enumOptions = { { "off", "Off" }, { "forward", "Forward" }, { "pingpong", "Ping-pong" },
                                                       { "held", "While held" } } },
                ParameterDescriptor { .id = "source.sampler.interpolation", .minValue = 0.0f, .maxValue = 2.0f, .defaultValue = 1.0f,
                                      .displayName = "Quality", .isInteger = true, .kind = ValueKind::Enum,
                                      .enumOptions = { { "linear", "Linear" }, { "cubic", "Cubic" }, { "sinc", "Sinc" } } },
                ParameterDescriptor { .id = "source.sampler.autoStart", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f,
                                      .displayName = "Play on start", .isInteger = true, .kind = ValueKind::Enum,
                                      .enumOptions = { { "no", "Wait for Start" }, { "yes", "When it starts" } } },
            };
        }

        void setParameter (const juce::String& id, float value) override
        {
            const auto asInt = (int) std::lround (value);
            if (id == "source.sampler.loop")
                loop = (Loop) juce::jlimit (0, 3, asInt);
            else if (id == "source.sampler.interpolation")
                interpolation = (Interpolation) juce::jlimit (0, 2, asInt);
            else if (id == "source.sampler.autoStart")
                autoStart = asInt != 0;
            else if (id == "source.sampler.position")
                storedPosition = value;
            else if (id == "source.sampler.rate")
                storedRate = value;
        }

        void setDataInput (const juce::String& portId, DataPublisher* publisher) noexcept override
        {
            if (portId == "sample")
                source = publisher;
        }

        void processBlock (const float* const* inputs, float* const* outputs, int numSamples) noexcept override
        {
            // Flat inputs: sample (Data, no buffer slot is read), then the rest.
            const auto in = [inputs] (int index, int s) noexcept { return inputs[index + 1][s]; };
            const auto* buffer = source != nullptr ? source->getCurrentForAudioThread() : nullptr;

            for (int s = 0; s < numSamples; ++s)
            {
                const auto start = in (startIn, s) > 0.0f;
                const auto stop = in (stopIn, s) > 0.0f;
                const auto gate = in (gateIn, s);
                const auto gateHigh = std::isnan (gate) || gate > 0.5f;
                auto end = false;

                if (buffer == nullptr || buffer->frames() < 1)
                {
                    write (outputs, s, 0.0f, 0.0f, false, 0.0f, false);
                    continue;
                }

                const auto frames = (double) buffer->frames();
                const auto& meta = buffer->sampleMeta;
                const auto position = readOr (in (positionIn, s), storedPosition);
                const auto rate = (double) readOr (in (rateIn, s), storedRate);
                const auto pitch = in (pitchIn, s);
                const auto root = std::isfinite (meta.rootPitch) ? meta.rootPitch : 60.0f;
                const auto speed = rate * (std::isnan (pitch) ? 1.0 : std::exp2 ((double) (pitch - root) / 12.0));
                const auto step = speed * buffer->sampleRate() / outputRate;

                if (start || pendingStart)
                {
                    pendingStart = false;
                    playing = true;
                    direction = 1.0;
                    sliceEnd = -1.0;
                    const auto slice = in (sliceIn, s);
                    if (! std::isnan (slice) && ! meta.slices.empty())
                    {
                        const auto n = juce::jlimit (0, (int) meta.slices.size(), (int) std::lround (slice));
                        const auto first = n == 0 ? 0 : meta.slices[(size_t) n - 1];
                        head = (double) first;
                        sliceEnd = n < (int) meta.slices.size() ? (double) meta.slices[(size_t) n] : frames;
                    }
                    else
                    {
                        // Backwards from Position, or from the end when Position is 0.
                        head = juce::jlimit (0.0, frames - 1.0, (double) position * frames);
                        if (step < 0.0 && position <= 0.0f)
                            head = frames - 1.0;
                    }
                }
                if (stop)
                    playing = false;

                // Rate 0: scrubbing — the head is wherever Position says.
                const auto scrubbing = std::abs (speed) < 1.0e-6;
                if (scrubbing)
                    head = juce::jlimit (0.0, frames - 1.0, (double) position * frames);

                float left = 0.0f, right = 0.0f;
                if (playing)
                {
                    read (*buffer, head, std::abs (step), in (channelIn, s), left, right);
                    if (! scrubbing)
                        end = advance (meta, frames, step, gateHigh);
                }
                gateWasHigh = gateHigh;
                write (outputs, s, left, right, playing, (float) (head / frames), end);
            }
        }

    private:
        static float readOr (float value, float fallback) noexcept { return std::isnan (value) ? fallback : value; }

        static void write (float* const* outputs, int s, float left, float right, bool playingNow, float position, bool end) noexcept
        {
            outputs[outLeft][s] = left;
            outputs[outRight][s] = right;
            outputs[gateOut][s] = playingNow ? 1.0f : 0.0f;
            outputs[positionOut][s] = position;
            outputs[endOut][s] = end ? 1.0f : 0.0f;
        }

        /** Moves the head one output sample; true at the moment it ends. */
        bool advance (const DataBuffer::SampleMeta& meta, double frames, double step, bool gateHigh) noexcept
        {
            const auto hasLoop = meta.loopStart >= 0 && meta.loopEnd > meta.loopStart;
            const auto loopStart = hasLoop ? (double) meta.loopStart : 0.0;
            const auto loopEnd = hasLoop ? (double) meta.loopEnd : frames;
            const auto looping = loop == Loop::Forward || loop == Loop::PingPong || (loop == Loop::WhileHeld && gateHigh);

            head += step * direction;

            if (sliceEnd >= 0.0 && head >= sliceEnd)
                return stopHere();

            if (looping && sliceEnd < 0.0)
            {
                const auto length = loopEnd - loopStart;
                if (loop == Loop::PingPong)
                {
                    if (head >= loopEnd)
                    {
                        head = loopEnd - (head - loopEnd);
                        direction = -direction;
                    }
                    else if (head < loopStart && direction * step < 0.0)
                    {
                        head = loopStart + (loopStart - head);
                        direction = -direction;
                    }
                }
                else if (head >= loopEnd)
                    head = loopStart + std::fmod (head - loopStart, length);
                else if (head < loopStart && step < 0.0)
                    head = loopEnd - std::fmod (loopStart - head, length);
                return false;
            }

            if (head >= frames || head < 0.0)
                return stopHere();
            return false;
        }

        bool stopHere() noexcept
        {
            playing = false;
            return true;
        }

        /** Two output channels at `position`, read from the band-limited copy
            that suits `speed`, with the chosen interpolation. */
        void read (const DataBuffer& buffer, double position, double speed, float channelSelect, float& left, float& right) const noexcept
        {
            const auto& meta = buffer.sampleMeta;
            auto level = 0;
            if (speed > 1.5) // each level halves the rate: play the one whose top is still under Nyquist
                level = juce::jlimit (0, (int) meta.mipLevels.size(), (int) std::floor (std::log2 (speed / 1.5)) + 1);
            const auto scale = (double) (1 << level);
            const auto* data = level == 0 ? buffer.rawData() : meta.mipLevels[(size_t) level - 1].data();
            const auto channels = buffer.channels();
            const auto frames = level == 0 ? buffer.frames() : (int) (meta.mipLevels[(size_t) level - 1].size() / (size_t) channels);
            const auto at = position / scale;

            const auto sampleAt = [&] (int channel) noexcept { return interpolate (data, frames, channels, channel, at); };

            if (! std::isnan (channelSelect))
            {
                const auto c = juce::jlimit (0.0f, (float) (channels - 1), channelSelect);
                const auto lo = (int) std::floor (c);
                const auto hi = juce::jmin (channels - 1, lo + 1);
                const auto t = c - (float) lo;
                left = right = sampleAt (lo) * (1.0f - t) + (hi != lo ? sampleAt (hi) * t : 0.0f);
                return;
            }
            left = sampleAt (0);
            right = channels > 1 ? sampleAt (1) : left;
        }

        float interpolate (const float* data, int frames, int channels, int channel, double position) const noexcept
        {
            const auto i = (int) std::floor (position);
            const auto t = (float) (position - (double) i);
            const auto at = [&] (int frame) noexcept { return frame >= 0 && frame < frames ? data[(size_t) frame * (size_t) channels + (size_t) channel] : 0.0f; };

            switch (interpolation)
            {
                case Interpolation::Linear:
                    return at (i) + (at (i + 1) - at (i)) * t;
                case Interpolation::Cubic:
                {
                    const auto y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
                    const auto c1 = 0.5f * (y2 - y0);
                    const auto c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
                    const auto c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                    return ((c3 * t + c2) * t + c1) * t + y1;
                }
                case Interpolation::Sinc:
                {
                    const auto& table = sincTable();
                    const auto phase = juce::jlimit (0, sincPhases - 1, (int) std::lround (t * (float) (sincPhases - 1)));
                    float sum = 0.0f;
                    for (int k = 0; k < sincTaps; ++k)
                        sum += at (i - sincTaps / 2 + 1 + k) * table[(size_t) phase * sincTaps + (size_t) k];
                    return sum;
                }
            }
            return 0.0f;
        }

        static constexpr int sincTaps = 8;
        static constexpr int sincPhases = 256;

        /** Lanczos (a = 4) weights for each of `sincPhases` fractional
            positions, normalised to sum to 1. Built once, at first use — on the
            message thread, since construction calls it (see the constructor). */
        static const std::vector<float>& sincTable()
        {
            static const std::vector<float> table = []
            {
                std::vector<float> weights ((size_t) sincPhases * sincTaps);
                for (int p = 0; p < sincPhases; ++p)
                {
                    const auto frac = (double) p / (double) (sincPhases - 1);
                    double sum = 0.0;
                    for (int k = 0; k < sincTaps; ++k)
                    {
                        const auto x = (double) (k - sincTaps / 2 + 1) - frac;
                        const auto sinc = [] (double v) { return std::abs (v) < 1.0e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * v) / (juce::MathConstants<double>::pi * v); };
                        const auto w = std::abs (x) < 4.0 ? sinc (x) * sinc (x / 4.0) : 0.0;
                        weights[(size_t) p * sincTaps + (size_t) k] = (float) w;
                        sum += w;
                    }
                    for (int k = 0; k < sincTaps; ++k)
                        weights[(size_t) p * sincTaps + (size_t) k] = (float) (weights[(size_t) p * sincTaps + (size_t) k] / sum);
                }
                return weights;
            }();
            return table;
        }

    public:
        SamplerNode() { sincTable(); } // build the table off the audio thread

    private:
        DataPublisher* source = nullptr;
        double outputRate = 48000.0;
        Loop loop = Loop::Off;
        Interpolation interpolation = Interpolation::Cubic;
        bool autoStart = true;
        float storedPosition = 0.0f, storedRate = 1.0f;

        bool playing = true, pendingStart = true, gateWasHigh = false;
        double head = 0.0, direction = 1.0, sliceEnd = -1.0;
    };
}
