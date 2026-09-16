#include "PluginEditor.h"
#include "bazalt/engine/telemetry/TelemetryFrame.h"
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

        std::optional<bazalt::engine::TelemetryFrameType> frameTypeFromPathSegment (const juce::String& segment)
        {
            if (segment == "scope")
                return bazalt::engine::TelemetryFrameType::Oscilloscope;
            if (segment == "spectrum")
                return bazalt::engine::TelemetryFrameType::Spectrum;
            if (segment == "meter")
                return bazalt::engine::TelemetryFrameType::Meter;
            return std::nullopt;
        }

        // ARCHITECTURE.md §6.3: "bazalt-tap://scope/main" is the
        // conceptual shape; JUCE's resource provider is one callback per
        // WebBrowserComponent differentiated by PATH within a single
        // virtual origin, not a literal custom URL scheme — so taps are
        // served at /tap/<name>/<scope|spectrum|meter> instead.
        std::optional<juce::WebBrowserComponent::Resource> serveTap (BazaltAudioProcessor& processor, const juce::String& url)
        {
            const auto remainder = url.fromFirstOccurrenceOf ("/tap/", false, false);
            const auto tapName = remainder.upToFirstOccurrenceOf ("/", false, false);
            const auto frameTypeSegment = remainder.fromFirstOccurrenceOf ("/", false, false);

            const auto frameType = frameTypeFromPathSegment (frameTypeSegment);
            if (tapName.isEmpty() || ! frameType.has_value())
                return std::nullopt;

            auto* buffer = processor.getTelemetryHub().getFrameBuffer (tapName, *frameType);
            if (buffer == nullptr)
                return std::nullopt;

            // Sized to comfortably exceed the largest frame AnalysisThread
            // ever publishes (the 2048-point spectrum: 1024 floats + header).
            std::vector<std::byte> data (16384);
            const auto numBytes = buffer->readLatest (data.data(), data.size());
            data.resize (numBytes);

            return juce::WebBrowserComponent::Resource { std::move (data), juce::String ("application/octet-stream") };
        }

        std::optional<juce::WebBrowserComponent::Resource> serveResource (BazaltAudioProcessor& processor, const juce::String& url)
        {
            if (url.startsWith ("/tap/"))
                return serveTap (processor, url);

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

    juce::WebBrowserComponent::Options BazaltAudioProcessorEditor::makeWebViewOptions (BazaltAudioProcessor& processor,
                                                                                       juce::WebSliderRelay& oscShapeRelay,
                                                                                       juce::WebSliderRelay& filterCutoffRelay,
                                                                                       juce::WebSliderRelay& filterResonanceRelay,
                                                                                       juce::WebSliderRelay& envReleaseRelay)
    {
        using Options = juce::WebBrowserComponent::Options;

        auto options = Options {}
                            .withBackend (Options::Backend::webview2)
                            .withWinWebView2Options (
                                Options::WinWebView2 {}.withUserDataFolder (
                                    juce::File::getSpecialLocation (juce::File::tempDirectory)
                                        .getChildFile ("Bazalt")
                                        .getChildFile ("WebView2")))
                            .withNativeIntegrationEnabled()
                            .withOptionsFrom (oscShapeRelay)
                            .withOptionsFrom (filterCutoffRelay)
                            .withOptionsFrom (filterResonanceRelay)
                            .withOptionsFrom (envReleaseRelay);

        auto provider = [&processor] (const juce::String& url) { return serveResource (processor, url); };

       #if JUCE_DEBUG
        options = options.withResourceProvider (provider, juce::URL (BAZALT_UI_DEV_SERVER_URL).getOrigin());
       #else
        options = options.withResourceProvider (provider);
       #endif

        return options;
    }

    BazaltAudioProcessorEditor::BazaltAudioProcessorEditor (BazaltAudioProcessor& p)
        : juce::AudioProcessorEditor (&p),
          processorRef (p),
          webView (makeWebViewOptions (p, oscShapeRelay, filterCutoffRelay, filterResonanceRelay, envReleaseRelay)),
          oscShapeAttachment (p.getMacroParameter (0), oscShapeRelay),
          filterCutoffAttachment (p.getMacroParameter (1), filterCutoffRelay),
          filterResonanceAttachment (p.getMacroParameter (2), filterResonanceRelay),
          envReleaseAttachment (p.getMacroParameter (3), envReleaseRelay)
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
