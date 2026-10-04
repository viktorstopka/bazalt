#pragma once

#include "bazalt/engine/graph/Node.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "view.tune" (design/Visualization/Tune.png) — the
        default viewer for Pitch-quantity Control signals: the nearest note
        and octave, the deviation in cents with a centre-zero meter, the raw
        semitones and the frequency. Spawned by Ctrl/Cmd-clicking a Pitch
        output port (graphStore.ts's DEFAULT_VIEWER_BY_PORT_KIND).

        A real pass-through, so it can sit in a pitch cable without changing
        it. Like view.count it needs no new telemetry: the declared Waveform
        preview on "out" is the ordinary Oscilloscope tap, and TuneBody.tsx
        reads its newest bucket as "the current pitch". Note, cents and Hz
        are all derived there from that ONE value — never from each other
        after rounding.
    */
    class ViewTuneNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 1; }

        juce::String getTitle() const override { return "Tune"; }
        juce::String getCategory() const override { return "View"; }
        NodeLayoutVariant getLayoutVariant() const override { return NodeLayoutVariant::Glance; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in", .type = SignalType::Control, .label = "In", .unit = "st",
                                      .minValue = 0.0f, .maxValue = 127.0f, .quantity = Quantity::Pitch } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Control, .label = "Out", .isPrimaryOutput = true,
                                      .unit = "st", .minValue = 0.0f, .maxValue = 127.0f, .quantity = Quantity::Pitch } };
        }

        std::vector<PreviewDescriptor> getPreviews() const override
        {
            return { PreviewDescriptor { .kind = PreviewKind::Waveform, .portId = "out" } };
        }

        void processSample (const float* inputs, float* outputs) noexcept override { outputs[0] = inputs[0]; }
    };
}
