#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

namespace bazalt
{
    /** Embeds a WebBrowserComponent pointed at the Vite dev server in debug
        builds, and a minimal placeholder page (served via a resource
        provider) in release builds until ui/dist is wired up for real
        (ARCHITECTURE.md §7). The resource provider is registered in BOTH
        configurations now (M4) so telemetry tap data
        (`/tap/<name>/<scope|spectrum|meter>`, ARCHITECTURE.md §6.3) is
        fetchable regardless of where the page itself loaded from — in
        debug, `allowedOriginIn` lets scripts loaded from the Vite dev
        server's origin reach the resource provider's separate virtual
        origin cross-origin.
    */
    class BazaltAudioProcessorEditor final : public juce::AudioProcessorEditor
    {
    public:
        explicit BazaltAudioProcessorEditor (BazaltAudioProcessor&);
        ~BazaltAudioProcessorEditor() override;

        void resized() override;

    private:
        static juce::WebBrowserComponent::Options makeWebViewOptions (BazaltAudioProcessor& processor);

        BazaltAudioProcessor& processorRef;
        juce::WebBrowserComponent webView;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BazaltAudioProcessorEditor)
    };
}
