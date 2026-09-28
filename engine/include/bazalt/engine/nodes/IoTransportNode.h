#pragma once

#include "bazalt/engine/graph/Node.h"
#include <algorithm>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** Stable type id: "io.transport" (NODE_CATALOG.md's `io.*` row): the
        host's timeline as graph signals, so clocks and LFOs can sync to it.
        No inputs; four outputs:
          - `beat`     Event  — one pulse on every beat (quarter note) of the
                        host's beat grid, on the exact sample the beat begins.
                        A beat that is also a bar's downbeat is just one of
                        these; count beats to find bars.
          - `tempo`    Control, Frequency — beats per second (BPM / 60), the
                        "BPM-derived frequency" the catalog asks for: an LFO
                        rate of `tempo` is one cycle per beat.
          - `playing`  Boolean — whether the transport is running.
          - `position` Control, Time — seconds since the timeline start; it
                        holds still while stopped.

        In the Standalone app there is no host: the plugin then runs an
        internal transport (120 BPM, playing) so these outputs still move —
        that decision lives in the plugin, since it depends on JUCE's playhead;
        this node just reads HostInputs (HostInputs.h) and can't tell.

        Beats are sample-accurate and split-proof: sample i fires when
        `floor(ppq at i) > floor(ppq at i-1)`, which depends only on absolute
        beat position, so a block split into sub-ranges at MIDI events (as the
        voice domain does) can neither drop a beat nor fire it twice.
        No beats while stopped or at a non-positive tempo.
    */
    class IoTransportNode : public Node
    {
    public:
        static constexpr int numOutputs = 4; // beat, tempo, playing, position

        int getNumInputPorts() const noexcept override { return 0; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Transport"; }
        juce::String getCategory() const override { return "IO"; } // not "I/O" - "/" is the Add menu's category-nesting delimiter (09-29-AddMenu.3)

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "beat", .type = SignalType::Event, .label = "Beat", .isPrimaryOutput = true },
                PortDescriptor { .id = "tempo", .type = SignalType::Control, .label = "Tempo", .unit = "Hz",
                                  .quantity = Quantity::Frequency },
                PortDescriptor { .id = "playing", .type = SignalType::Boolean, .label = "Playing", .kind = ValueKind::Bool },
                PortDescriptor { .id = "position", .type = SignalType::Control, .label = "Position", .unit = "s",
                                  .quantity = Quantity::Time },
            };
        }

        bool supportsPerSample() const noexcept override { return false; }

        bool wantsHostInputs() const noexcept override { return true; }

        void setHostInputs (const HostInputs& inputs) noexcept override
        {
            playing = inputs.transportPlaying;
            tempoBpm = inputs.tempoBpm;
            ppqAtStart = inputs.ppqPosition;
            timeAtStart = inputs.timeSeconds;
            sampleRate = inputs.sampleRate;
        }

        void processBlock (const float* const*, float* const* outputs, int numSamples) noexcept override
        {
            auto* beat = outputs[0];
            auto* tempo = outputs[1];
            auto* playingOut = outputs[2];
            auto* position = outputs[3];

            const auto beatsPerSecond = tempoBpm > 0.0 ? tempoBpm / 60.0 : 0.0;
            std::fill (tempo, tempo + numSamples, (float) beatsPerSecond);
            std::fill (playingOut, playingOut + numSamples, playing ? 1.0f : 0.0f);

            const auto beatsPerSample = sampleRate > 0.0 ? beatsPerSecond / sampleRate : 0.0;
            const auto running = playing && beatsPerSample > 0.0;

            for (int i = 0; i < numSamples; ++i)
            {
                position[i] = (float) (running ? timeAtStart + (double) i / sampleRate : timeAtStart);

                if (! running)
                {
                    beat[i] = 0.0f;
                    continue;
                }

                // The epsilon keeps a beat that lands exactly on a sample
                // from flickering across the boundary with float error.
                const auto here = std::floor (ppqAtStart + (double) i * beatsPerSample + 1.0e-9);
                const auto before = std::floor (ppqAtStart + (double) (i - 1) * beatsPerSample + 1.0e-9);
                beat[i] = here > before ? 1.0f : 0.0f;
            }
        }

    private:
        bool playing = false;
        double tempoBpm = 120.0;
        double ppqAtStart = 0.0;
        double timeAtStart = 0.0;
        double sampleRate = 44100.0;
    };
}
