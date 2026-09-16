#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

namespace bazalt
{
    /** M0 placeholder: embeds a WebBrowserComponent pointed at the Vite dev
        server in debug builds, and a minimal placeholder page (served via a
        resource provider) in release builds until ui/dist is wired up for
        real (ARCHITECTURE.md §7). No parameter/telemetry bridge yet — that's
        M3/M4/M5.
    */
    class BazaltAudioProcessorEditor final : public juce::AudioProcessorEditor
    {
    public:
        explicit BazaltAudioProcessorEditor (BazaltAudioProcessor&);
        ~BazaltAudioProcessorEditor() override;

        void resized() override;

    private:
        static juce::WebBrowserComponent::Options makeWebViewOptions();

        BazaltAudioProcessor& processorRef;
        juce::WebBrowserComponent webView;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BazaltAudioProcessorEditor)
    };
}
