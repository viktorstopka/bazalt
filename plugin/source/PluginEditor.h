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
        static juce::WebBrowserComponent::Options makeWebViewOptions (BazaltAudioProcessor& processor,
                                                                       juce::WebSliderRelay& oscShapeRelay,
                                                                       juce::WebSliderRelay& filterCutoffRelay,
                                                                       juce::WebSliderRelay& filterResonanceRelay,
                                                                       juce::WebSliderRelay& envReleaseRelay);

        // M7 command bridge (NODE_EDITOR.md §6): registers addNode/
        // deleteNode/connect/disconnect/setParameterValue as native
        // functions the WebView can call via getNativeFunction(name) —
        // see GraphEditController for what each one actually does. No UI
        // calls these yet (M7's own scope: driven by a test harness, not
        // real UI); this just completes the transport so M10's real UI has
        // something to call into.
        static juce::WebBrowserComponent::Options withGraphCommands (juce::WebBrowserComponent::Options options,
                                                                      BazaltAudioProcessor& processor);

        BazaltAudioProcessor& processorRef;

        // M5: the 4 macros ARCHITECTURE.md §4.3/M3 already maps to the
        // hardcoded voice graph (osc shape, filter cutoff/resonance, env
        // release) exposed as real host-automatable controls, via JUCE's
        // built-in Web*Relay/Web*ParameterAttachment mechanism rather than
        // the M7+ command bridge (NODE_EDITOR.md §6) — that bridge is for
        // graph-editing commands; a plain parameter round-trip already has
        // a first-party JUCE mechanism and doesn't need a custom one.
        // Relays must outlive webView (constructed first, referenced by
        // makeWebViewOptions' withOptionsFrom chain); attachments must be
        // constructed after webView so their initial update has a live
        // browser to reach — declaration order below is deliberate.
        juce::WebSliderRelay oscShapeRelay { "oscShape" };
        juce::WebSliderRelay filterCutoffRelay { "filterCutoff" };
        juce::WebSliderRelay filterResonanceRelay { "filterResonance" };
        juce::WebSliderRelay envReleaseRelay { "envRelease" };

        juce::WebBrowserComponent webView;

        juce::WebSliderParameterAttachment oscShapeAttachment;
        juce::WebSliderParameterAttachment filterCutoffAttachment;
        juce::WebSliderParameterAttachment filterResonanceAttachment;
        juce::WebSliderParameterAttachment envReleaseAttachment;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BazaltAudioProcessorEditor)
    };
}
