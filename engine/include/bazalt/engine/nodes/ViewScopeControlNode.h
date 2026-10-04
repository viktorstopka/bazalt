#pragma once

#include "bazalt/engine/nodes/ViewHistoryWindow.h"

namespace bazalt::engine::nodes
{
    /** Stable type id: "view.scope.control" (design/Visualization/Scope1.png)
        — the default long-window scrolling-history viewer for plain Control
        signals (not integer, not Modulation-quantity — those get their own
        variants: view.count, view.scope.modulation and view.gate — the
        latter two share this node's whole time-window mechanism through
        ViewHistoryWindow.h).

        Replaces view.glance's own role as "the thing you'd reach for to
        watch a slow Control signal": Glance's Waveform preview inherits the
        Oscilloscope tap's hard cap at the tap ring's own raw-sample depth
        (~0.2s at 44.1kHz — ViewScopeNode.h's own doc comment calls this out
        explicitly as a known, deliberately-deferred gap), which reads as a
        flat line or a blur for anything slower than that. This node's own
        preview (PreviewKind::RollingHistory, AnalysisThread::
        publishRollingHistory) is a genuinely different, incrementally-built
        mechanism with no such cap — see that function's own comment.

        Fixed Control ports, not polymorphic the way view.glance is — a
        viewer for plain Control signals specifically, same reasoning
        ViewCountNode/ViewRippleNode give for staying fixed to their own
        type rather than adopting whatever's wired. A real pass-through (in
        AND out) so it can be spliced into an existing wire without altering
        behaviour.

        `timeWindow` is its one real, engine-tracked setting — editable
        in-panel (design/Visualization/Scope1.png's own small numbers
        outside the panel), not a generic ParameterRow, but still a REAL
        ParameterDescriptor/setParameter underneath: editing it goes through
        the exact same recompile -> re-attach -> TapSettings::fromPreview
        path every other per-instance preview setting (view.scope's own
        timeWindow/trigger, ViewNodes.h) already uses — ScopeControlBody.tsx's
        own auto-window heuristic commits its computed default through this
        same parameter, not a cosmetic property, since it genuinely changes
        what AnalysisThread computes. Bounds (0.01s-30s) match
        AnalysisThread::minHistoryWindowSeconds/maxHistoryWindowSeconds —
        duplicated here rather than shared from that class the same way
        ViewScopeNode's own 1-150ms bounds are independently hardcoded
        there, not read from TapSettings; AnalysisThread clamps its own
        input regardless, so a mismatch here could only ever be a UI-side
        display-range nicety, never a correctness issue.

        The vertical range (Min/Max) is NOT a parameter — unlike the time
        window, it has no telemetry-shape consequence at all (purely a
        display scale), so it lives in NodeInstance.properties instead, same
        "cosmetic goes in properties, DSP-affecting goes in parameters"
        split view.count's own Min/Max already established
        (ui/src/graph/graphStore.ts's setViewerRange).
    */
    class ViewScopeControlNode : public Node
    {
    public:
        static constexpr int numInputs = 1;
        static constexpr int numOutputs = 1;
        static constexpr float minTimeWindowSeconds = ViewHistoryWindow::minSeconds;
        static constexpr float maxTimeWindowSeconds = ViewHistoryWindow::maxSeconds;
        static constexpr float defaultTimeWindowSeconds = ViewHistoryWindow::defaultSeconds;

        int getNumInputPorts() const noexcept override { return numInputs; }
        int getNumOutputPorts() const noexcept override { return numOutputs; }

        // No title (design/Visualization/Scope1.png) — ScopeControlBody.tsx
        // never reads this, but a real, sensible value still matters for
        // the Add menu/component gallery (ViewRippleNode.h's own reasoning).
        juce::String getTitle() const override { return "Scope"; }
        juce::String getCategory() const override { return "View"; }
        NodeLayoutVariant getLayoutVariant() const override { return NodeLayoutVariant::Glance; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in", .type = SignalType::Control, .label = "In" } };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { PortDescriptor { .id = "out", .type = SignalType::Control, .label = "Out", .isPrimaryOutput = true } };
        }

        std::vector<ParameterDescriptor> getParameters() const override { return { window.getParameter() }; }
        void setParameter (const juce::String& parameterId, float value) override { window.setParameter (parameterId, value); }
        std::vector<PreviewDescriptor> getPreviews() const override { return { window.getPreview ("out") }; }

        void processSample (const float* inputs, float* outputs) noexcept override { outputs[0] = inputs[0]; }

    private:
        ViewHistoryWindow window { "view.scope.control.timeWindow" };
    };
}
