#pragma once

#include "bazalt/engine/graph/Node.h"
#include <algorithm>

namespace bazalt::engine::nodes
{
    /** The one setting every scrolling-history viewer shares
        (design/Visualization/Scope1.png, ScopeMod.png, Gate.png): an
        editable time window that sizes AnalysisThread::publishRollingHistory's
        columns. view.scope.control, view.scope.modulation and view.gate are
        "variants of one panel with different vertical scales and different
        trace styles" — their engine sides differ only in their ports, so the
        window itself lives here once and each node composes it, rather than
        three copies of the same parameter/clamp/preview code drifting apart.

        Bounds (0.01s-30s) match AnalysisThread::minHistoryWindowSeconds/
        maxHistoryWindowSeconds — duplicated rather than shared, the same way
        ViewScopeNode's own 1-150ms bounds are; AnalysisThread clamps its own
        input regardless, so a mismatch here could only ever be a UI-side
        display-range nicety, never a correctness issue.

        A real, structural ParameterDescriptor (not a cosmetic property): it
        changes what AnalysisThread computes, so an edit goes through the
        ordinary recompile -> re-attach -> TapSettings::fromPreview path.
    */
    struct ViewHistoryWindow
    {
        static constexpr float minSeconds = 0.01f;
        static constexpr float maxSeconds = 30.0f;
        static constexpr float defaultSeconds = 2.0f; // a starting guess; ScopeHistoryBody.tsx's own auto-window
                                                       // heuristic usually overwrites it within ~1s of a real connection

        explicit ViewHistoryWindow (juce::String parameterIdToUse) : parameterId (std::move (parameterIdToUse)) {}

        ParameterDescriptor getParameter() const
        {
            return ParameterDescriptor { .id = parameterId,
                                         .minValue = minSeconds,
                                         .maxValue = maxSeconds,
                                         .defaultValue = defaultSeconds,
                                         .skew = 0.3f,
                                         .unit = "s",
                                         .displayName = "Time Window",
                                         .quantity = Quantity::Time,
                                         .curve = Curve::Logarithmic,
                                         .isStructural = true };
        }

        /** Returns true if `id` was this window's parameter. */
        bool setParameter (const juce::String& id, float value) noexcept
        {
            if (id != parameterId)
                return false;
            seconds = std::clamp (value, minSeconds, maxSeconds);
            return true;
        }

        PreviewDescriptor getPreview (const juce::String& portId) const
        {
            return PreviewDescriptor { .kind = PreviewKind::RollingHistory, .portId = portId, .timeWindowSeconds = seconds };
        }

        juce::String parameterId;
        float seconds = defaultSeconds;
    };
}
