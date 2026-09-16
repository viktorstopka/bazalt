#include "PluginEditor.h"
#include <optional>

namespace bazalt
{
    namespace
    {
        constexpr int defaultWidth = 900;
        constexpr int defaultHeight = 600;

        constexpr char placeholderHtml[] = R"html(<!doctype html>
<html>
  <head>
    <meta charset="utf-8" />
    <title>Bazalt</title>
    <style>
      html, body { margin: 0; height: 100%; background: #14161a; color: #e8e8ea;
        font-family: -apple-system, Segoe UI, sans-serif; display: flex;
        align-items: center; justify-content: center; }
      p { opacity: 0.6; }
    </style>
  </head>
  <body>
    <p>Bazalt &mdash; placeholder view. Build ui/dist for the real UI.</p>
  </body>
</html>
)html";

        std::optional<juce::WebBrowserComponent::Resource> servePlaceholderPage (const juce::String& url)
        {
            if (url == "/" || url == "/index.html")
            {
                const auto* begin = reinterpret_cast<const std::byte*> (placeholderHtml);
                const auto* end = begin + (sizeof (placeholderHtml) - 1);

                return juce::WebBrowserComponent::Resource { std::vector<std::byte> (begin, end),
                                                              juce::String ("text/html") };
            }

            return std::nullopt;
        }
    }

    juce::WebBrowserComponent::Options BazaltAudioProcessorEditor::makeWebViewOptions()
    {
        using Options = juce::WebBrowserComponent::Options;

        auto options = Options {}
                            .withBackend (Options::Backend::webview2)
                            .withWinWebView2Options (
                                Options::WinWebView2 {}.withUserDataFolder (
                                    juce::File::getSpecialLocation (juce::File::tempDirectory)
                                        .getChildFile ("Bazalt")
                                        .getChildFile ("WebView2")))
                            .withNativeIntegrationEnabled();

       #if ! JUCE_DEBUG
        options = options.withResourceProvider (servePlaceholderPage);
       #endif

        return options;
    }

    BazaltAudioProcessorEditor::BazaltAudioProcessorEditor (BazaltAudioProcessor& p)
        : juce::AudioProcessorEditor (&p),
          processorRef (p),
          webView (makeWebViewOptions())
    {
        addAndMakeVisible (webView);
        setResizable (true, true);
        setSize (defaultWidth, defaultHeight);

       #if JUCE_DEBUG
        webView.goToURL (BAZALT_UI_DEV_SERVER_URL);
       #else
        webView.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
       #endif
    }

    BazaltAudioProcessorEditor::~BazaltAudioProcessorEditor() = default;

    void BazaltAudioProcessorEditor::resized()
    {
        webView.setBounds (getLocalBounds());
    }
}
