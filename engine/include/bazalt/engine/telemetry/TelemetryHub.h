#pragma once

#include "bazalt/engine/telemetry/Tap.h"
#include "bazalt/engine/telemetry/TelemetryFrame.h"
#include "bazalt/engine/telemetry/TelemetryFrameBuffer.h"
#include <juce_core/juce_core.h>
#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

namespace bazalt::engine
{
    /** Owns the fixed set of named taps and their per-frame-type published
        buffers. The set of tap names is fixed at prepare() time (matching
        M4's scope: taps wired to main output and the 4 sidechain inputs,
        ARCHITECTURE.md's "any point in the graph" generality is a later
        refinement once there's a node-graph editor to attach taps from).
    */
    class TelemetryHub
    {
    public:
        void prepare (const std::vector<juce::String>& tapNames, size_t tapCapacity, size_t maxFrameBytes)
        {
            tapNamesOrdered = tapNames;

            for (const auto& name : tapNames)
            {
                auto tap = std::make_unique<Tap>();
                tap->prepare (tapCapacity);
                taps[name] = std::move (tap);

                auto& buffers = frameBuffers[name];
                for (auto& buffer : buffers)
                {
                    buffer = std::make_unique<TelemetryFrameBuffer>();
                    buffer->prepare (maxFrameBytes);
                }
            }
        }

        const std::vector<juce::String>& getTapNames() const noexcept { return tapNamesOrdered; }

        Tap* getTap (const juce::String& name) noexcept
        {
            const auto it = taps.find (name);
            return it == taps.end() ? nullptr : it->second.get();
        }

        TelemetryFrameBuffer* getFrameBuffer (const juce::String& name, TelemetryFrameType type) noexcept
        {
            const auto it = frameBuffers.find (name);
            if (it == frameBuffers.end())
                return nullptr;

            return it->second[(size_t) type].get();
        }

        const TelemetryFrameBuffer* getFrameBuffer (const juce::String& name, TelemetryFrameType type) const noexcept
        {
            const auto it = frameBuffers.find (name);
            if (it == frameBuffers.end())
                return nullptr;

            return it->second[(size_t) type].get();
        }

    private:
        std::vector<juce::String> tapNamesOrdered;
        std::unordered_map<juce::String, std::unique_ptr<Tap>> taps;
        std::unordered_map<juce::String, std::array<std::unique_ptr<TelemetryFrameBuffer>, 3>> frameBuffers;
    };
}
