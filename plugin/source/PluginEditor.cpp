#include "PluginEditor.h"
#include "NodeDescriptorJson.h"
#include "bazalt/engine/patch/PatchSerializer.h"
#include "bazalt/engine/telemetry/TelemetryFrame.h"
#include <optional>

namespace bazalt
{
    namespace
    {
        // Floor for the computed default below, and the fallback if no
        // display info is available at all.
        constexpr int minWidth = 900;
        constexpr int minHeight = 600;

        // Opens at ~80% of the primary display's usable area rather than a
        // fixed pixel size, so the window is immediately usable without a
        // resize drag — a real accessibility need (a pointing-device bug
        // that can't reliably grab the window edge), not just a cosmetic
        // default. setResizable() below still lets a working pointer
        // resize it further.
        juce::Rectangle<int> defaultEditorSize()
        {
            const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
            const auto userArea = display != nullptr ? display->userBounds.toNearestInt() : juce::Rectangle<int> (0, 0, minWidth, minHeight);
            constexpr float screenFraction = 0.8f;
            return { juce::jmax (minWidth, (int) (userArea.getWidth() * screenFraction)),
                     juce::jmax (minHeight, (int) (userArea.getHeight() * screenFraction)) };
        }

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

        juce::var commandResultToVar (const GraphEditController::CommandResult& result)
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("success", result.success);
            obj->setProperty ("errorMessage", result.errorMessage);
            return juce::var (obj);
        }

        // args[i].toString() covers every command parameter's JS type
        // (string ids, and numbers — var::toString() formats a double
        // without scientific notation for the ranges these commands use).
        // Numeric args go through (float) var directly instead, since
        // toString()+parse would lose no precision but reads oddly.
        juce::String argString (const juce::Array<juce::var>& args, int index)
        {
            return index < args.size() ? args[index].toString() : juce::String();
        }

        float argFloat (const juce::Array<juce::var>& args, int index)
        {
            return index < args.size() ? (float) args[index] : 0.0f;
        }

        // setProperty's value can be any JSON-shaped var (a rename's string,
        // a bypass flag's bool, a future property's number) — unlike the
        // other commands' typed args above, this one passes it through
        // as-is, matching NodeInstance::properties' own juce::var storage.
        juce::var argVar (const juce::Array<juce::var>& args, int index)
        {
            return index < args.size() ? args[index] : juce::var();
        }
    }

    juce::WebBrowserComponent::Options BazaltAudioProcessorEditor::withGraphCommands (
        juce::WebBrowserComponent::Options options, BazaltAudioProcessor& processor)
    {
        using Args = const juce::Array<juce::var>&;
        using Completion = juce::WebBrowserComponent::NativeFunctionCompletion;

        options = options.withNativeFunction ("graphAddNode", [&processor] (Args args, Completion completion)
        {
            auto& controller = processor.getGraphEditController();
            const auto result = controller.addNode (argString (args, 0), argString (args, 1),
                                                      argFloat (args, 2), argFloat (args, 3));
            completion (commandResultToVar (result));
        });

        options = options.withNativeFunction ("graphDeleteNode", [&processor] (Args args, Completion completion)
        {
            auto& controller = processor.getGraphEditController();
            const auto result = controller.deleteNode (argString (args, 0));
            completion (commandResultToVar (result));
        });

        options = options.withNativeFunction ("graphConnect", [&processor] (Args args, Completion completion)
        {
            auto& controller = processor.getGraphEditController();
            const auto result = controller.connect (argString (args, 0), argString (args, 1),
                                                      argString (args, 2), argString (args, 3));
            completion (commandResultToVar (result));
        });

        options = options.withNativeFunction ("graphDisconnect", [&processor] (Args args, Completion completion)
        {
            auto& controller = processor.getGraphEditController();
            const auto result = controller.disconnect (argString (args, 0), argString (args, 1),
                                                         argString (args, 2), argString (args, 3));
            completion (commandResultToVar (result));
        });

        options = options.withNativeFunction ("graphSetParameterValue", [&processor] (Args args, Completion completion)
        {
            auto& controller = processor.getGraphEditController();
            const auto result = controller.setParameterValue (argString (args, 0), argString (args, 1), argFloat (args, 2));
            completion (commandResultToVar (result));
        });

        options = options.withNativeFunction ("graphSetOutput", [&processor] (Args args, Completion completion)
        {
            auto& controller = processor.getGraphEditController();
            const auto result = controller.setOutput (argString (args, 0), argString (args, 1));
            completion (commandResultToVar (result));
        });

        options = options.withNativeFunction ("graphMoveNode", [&processor] (Args args, Completion completion)
        {
            auto& controller = processor.getGraphEditController();
            const auto result = controller.moveNode (argString (args, 0), argFloat (args, 1), argFloat (args, 2));
            completion (commandResultToVar (result));
        });

        options = options.withNativeFunction ("graphSetProperty", [&processor] (Args args, Completion completion)
        {
            auto& controller = processor.getGraphEditController();
            const auto result = controller.setProperty (argString (args, 0), argString (args, 1), argVar (args, 2));
            completion (commandResultToVar (result));
        });

        // M19 (ADR-0025): the UI predicts canConnect (a hand-mirrored TS
        // port, ui/src/graph/canConnect.ts) for live wire-drag feedback, but
        // committing a connection always goes through the real engine
        // decision — connectWithAutoAdapt (M16) so a real NeedsAdapters
        // outcome actually inserts the adapter chain, not just a flat
        // connect that would reject it.
        options = options.withNativeFunction ("graphConnectWithAutoAdapt", [&processor] (Args args, Completion completion)
        {
            auto& controller = processor.getGraphEditController();
            const auto result = controller.connectWithAutoAdapt (argString (args, 0), argString (args, 1),
                                                                   argString (args, 2), argString (args, 3));
            completion (commandResultToVar (result));
        });

        // M19 (ADR-0025): undo/redo is a client-side history of whole-graph
        // snapshots, not a per-command inverse log — graphGetSnapshot/
        // graphRestoreSnapshot are its only two moving parts, both reusing
        // the M0-M8 PatchDocument/PatchSerializer round-trip already proven
        // for real patch save/load (a graph-only snapshot is just a
        // PatchDocument with empty macro/view/meta fields nothing reads
        // back out of it).
        options = options.withNativeFunction ("graphGetSnapshot", [&processor] (Args, Completion completion)
        {
            auto& controller = processor.getGraphEditController();
            const auto json = bazalt::engine::serializePatchToJson (
                bazalt::engine::PatchDocument::fromNodeGraph (controller.getGraph()), false);
            completion (json);
        });

        options = options.withNativeFunction ("graphRestoreSnapshot", [&processor] (Args args, Completion completion)
        {
            const auto parsed = bazalt::engine::parsePatchFromJson (argString (args, 0));
            if (! parsed.success)
            {
                completion (commandResultToVar ({ false, parsed.errorMessage }));
                return;
            }

            auto& controller = processor.getGraphEditController();
            const auto result = controller.setGraph (parsed.document.toNodeGraph());
            completion (commandResultToVar (result));
        });

        // M9 (NODE_EDITOR.md §3): every registered node type's descriptor,
        // fetched once at editor load — not a graph-editing command (no
        // NodeGraph mutation, no recompile), but still goes over this
        // native-function transport rather than the telemetry fetch()
        // path, matching ADR-0006's RPC-shaped-vs-pull-based distinction.
        options = options.withNativeFunction ("getNodeDescriptors", [&processor] (Args, Completion completion)
        {
            completion (nodeDescriptorsToVar (processor.getNodeFactory().describeAll()));
        });

        // M8 (NODE_EDITOR.md §9): dynamic, viewport-driven tap subscription
        // — not a graph-editing command (no recompile involved), so these
        // don't go through GraphEditController. Always "succeeds" from the
        // caller's perspective (TelemetryHub::subscribeTap always returns a
        // tap, LRU-evicting if the pool is full); there's nothing to reject.
        options = options.withNativeFunction ("telemetrySubscribeTap", [&processor] (Args args, Completion completion)
        {
            processor.getTelemetryHub().subscribeTap (argString (args, 0));
            completion (true);
        });

        options = options.withNativeFunction ("telemetryUnsubscribeTap", [&processor] (Args args, Completion completion)
        {
            processor.getTelemetryHub().unsubscribeTap (argString (args, 0));
            completion (true);
        });

        return options;
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

        options = withGraphCommands (std::move (options), processor);

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
        const auto size = defaultEditorSize();
        setSize (size.getWidth(), size.getHeight());

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
