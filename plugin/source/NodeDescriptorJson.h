#pragma once

#include "bazalt/engine/graph/NodeDescriptor.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace bazalt
{
    /** Serializes NodeDescriptor (NODE_EDITOR.md §3) to the exact JSON
        shape ui/src/graph/descriptorTypes.ts expects — "one schema, two
        producers" only holds if both sides agree field-for-field, so this
        is the one place that mapping is written down. Message-thread only
        (same as NodeFactory::describeAll(), which is its only caller
        besides tests) — never touched by the audio thread.

        SignalType/NodeLayoutVariant serialize to the same lowercase
        strings descriptorTypes.ts's SignalType/NodeLayoutVariant unions
        use; optional<float> (minValue/maxValue) serializes to JS `null`
        when unset via a default-constructed juce::var, never to 0 or an
        omitted key, so the UI can't mistake "no bound" for "bound at zero".
    */
    juce::var nodeDescriptorToVar (const bazalt::engine::NodeDescriptor& descriptor);
    juce::var nodeDescriptorsToVar (const std::vector<bazalt::engine::NodeDescriptor>& descriptors);

    /** M20 — the reverse of previewKindToString's internal mapping (this
        file's own .cpp), needed at the one place a preview kind travels
        the other direction: the UI passing which kind it's subscribing a
        tap for (PluginEditor.cpp's subscribeNodePreviewTap), so
        AnalysisThread only computes the frame type(s) that kind actually
        reads. Defaults to Waveform for an unrecognized string rather than
        asserting — a native-function argument is untrusted input, not an
        internal invariant.
    */
    bazalt::engine::PreviewKind previewKindFromString (const juce::String& kind);
}
