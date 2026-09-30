#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include <memory>
#include <vector>

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
        static juce::WebBrowserComponent::Options makeWebViewOptions (
            BazaltAudioProcessor& processor,
            std::vector<std::unique_ptr<juce::WebSliderRelay>>& macroRelays);

        // M7 command bridge (NODE_EDITOR.md §6): registers addNode/
        // deleteNode/connect/disconnect/setParameterValue as native
        // functions the WebView can call via getNativeFunction(name) —
        // see GraphEditController for what each one actually does. Still no
        // UI caller for the graph-editing commands themselves (that's
        // M10's job); getNodeDescriptors (M9) is the first of these the UI
        // actually calls, fetched once by the component gallery at load.
        static juce::WebBrowserComponent::Options withGraphCommands (juce::WebBrowserComponent::Options options,
                                                                      BazaltAudioProcessor& processor);

        // wiki/plans/UtilMacro.md: one relay/attachment per MacroParameters::
        // numMacros (32) slot, not just the 4 ARCHITECTURE.md §4.3/M3
        // originally hardcoded — a real util.macro node can claim any of
        // the 32, so the editor needs a live WebView binding for all of
        // them, not just the first 4. Built via the static helpers below
        // (WebSliderRelay isn't default-constructible, so a plain
        // std::vector<WebSliderRelay> can't be declared+resized in the
        // constructor body the way a POD vector could) rather than 32
        // hand-written named members.
        static std::vector<std::unique_ptr<juce::WebSliderRelay>> makeMacroRelays();
        static std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>> makeMacroAttachments (
            BazaltAudioProcessor& processor, std::vector<std::unique_ptr<juce::WebSliderRelay>>& relays);

        BazaltAudioProcessor& processorRef;

        // M5/wiki/plans/UtilMacro.md: exposed as real host-automatable
        // controls via JUCE's built-in Web*Relay/Web*ParameterAttachment
        // mechanism rather than the M7+ command bridge (NODE_EDITOR.md §6)
        // — that bridge is for graph-editing commands; a plain parameter
        // round-trip already has a first-party JUCE mechanism and doesn't
        // need a custom one. macroRelays must outlive AND precede webView
        // in declaration order (constructed first, referenced by
        // makeWebViewOptions' withOptionsFrom chain); macroAttachments must
        // be declared after webView so their initial update has a live
        // browser to reach — declaration order below is deliberate, same
        // requirement the old 4 named members had.
        std::vector<std::unique_ptr<juce::WebSliderRelay>> macroRelays;

        juce::WebBrowserComponent webView;

        std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>> macroAttachments;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BazaltAudioProcessorEditor)
    };
}
