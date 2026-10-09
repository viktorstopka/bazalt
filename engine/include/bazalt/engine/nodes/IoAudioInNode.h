#pragma once

#include "bazalt/engine/graph/Node.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace bazalt::engine::nodes
{
    /** Stable type id: "io.audioIn" (NODE_CATALOG.md's `io.*` row): the
        plugin's audio input as graph signals. No inputs; two mono Audio
        outputs `channel.0`/`channel.1` (left/right) carrying whichever host
        bus the structural `bus` setting selects — Main, or one of the four
        sidechain buses Aux 1-4.

        The catalog describes `channel.0…channel.N` "sized by the selected
        bus". Every bus this plugin exposes is stereo (BusLayout rejects any
        other layout — ARCHITECTURE.md §4.1), so the count is always two, and
        this declares two fixed ports rather than a growable output group,
        which the engine doesn't have (growable groups are input groups,
        sized from incoming connections — PortGroups.h). If a mono or
        surround bus is ever allowed this is where the count would follow it.

        Behaviour: plain pass-through of the host's samples; silence when the
        selected bus isn't active in the host (a host that never enabled the
        sidechains hands over no data, which is normal, not an error).
        Receives its data through `setHostInputs()` (HostInputs.h), valid
        only for the one `process()` call it precedes — it copies what it
        needs and drops the pointers afterwards, so a `process()` with no
        fresh `applyHostInputs()` reads silence instead of a stale, possibly
        freed, host buffer.

        Domain: a mono source. Placed among voice-domain nodes it is
        duplicated per voice and every copy reads the same host samples
        (DOMAINS.md's free mono->poly broadcast); a graph with no
        `life.voice` runs once, every block, whether or not a note is
        held (DomainSplitter.h's mono-graph rule), which is what an audio
        effect needs.
    */
    class IoAudioInNode : public Node
    {
    public:
        static constexpr int numOutputs = 2; // channel.0, channel.1

        int getNumInputPorts() const noexcept override { return 0; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        juce::String getTitle() const override { return "Audio In"; }
        juce::String getCategory() const override { return "IO"; } // not "I/O" - "/" is the Add menu's category-nesting delimiter (09-29-AddMenu.3)

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return {
                PortDescriptor { .id = "channel.0", .type = SignalType::Signal, .label = "Left", .isPrimaryOutput = true, .quantity = Quantity::Audio },
                PortDescriptor { .id = "channel.1", .type = SignalType::Signal, .label = "Right", .quantity = Quantity::Audio },
            };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "io.audioIn.bus",
                                            .minValue = 0.0f,
                                            .maxValue = 4.0f,
                                            .defaultValue = 0.0f,
                                            .displayName = "Bus",
                                            .isInteger = true,
                                            .kind = ValueKind::Enum,
                                            .enumOptions = { { "main", "Main" }, { "aux1", "Aux 1" }, { "aux2", "Aux 2" },
                                                              { "aux3", "Aux 3" }, { "aux4", "Aux 4" } },
                                            .isStructural = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "io.audioIn.bus")
                bus = juce::jlimit (0, HostInputs::numAudioBuses - 1, (int) std::lround (value));
        }

        // A block-rate copy from a host pointer — no per-sample form (and a
        // node with no inputs can never sit inside a feedback cycle anyway).
        bool supportsPerSample() const noexcept override { return false; }

        bool wantsHostInputs() const noexcept override { return true; }

        void setHostInputs (const HostInputs& inputs) noexcept override
        {
            for (int ch = 0; ch < HostInputs::channelsPerBus; ++ch)
                channelSource[(size_t) ch] = inputs.audio[(size_t) bus][(size_t) ch];
        }

        void processBlock (const float* const*, float* const* outputs, int numSamples) noexcept override
        {
            for (int ch = 0; ch < numOutputs; ++ch)
            {
                if (const auto* source = channelSource[(size_t) ch])
                    std::memcpy (outputs[ch], source, sizeof (float) * (size_t) numSamples);
                else
                    std::fill (outputs[ch], outputs[ch] + numSamples, 0.0f);

                channelSource[(size_t) ch] = nullptr; // valid for this one call only
            }
        }

    private:
        int bus = 0;
        std::array<const float*, HostInputs::channelsPerBus> channelSource {};
    };
}
