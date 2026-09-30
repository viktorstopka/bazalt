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

            // The largest frame AnalysisThread can ever publish (an 8192-point
            // spectrum, ADR-0029) - the same constant the frame buffers are sized by.
            std::vector<std::byte> data (bazalt::engine::maxTelemetryFrameBytes);
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

        // Dev-convenience export (direct instruction, "build that" — a real
        // fix for "does Claude have quick access to the patch I'm
        // building" beyond describing it in words every time). Writes the
        // SAME PatchDocument JSON graphGetSnapshot produces, pretty-printed,
        // to a fixed file next to the project itself
        // (juce::File::getCurrentWorkingDirectory() — this project's own
        // documented launch convention always runs the Standalone/plugin
        // from the repo root, CLAUDE.md's own build/test/render commands
        // section) — never a build-time-baked absolute path. Overwrites
        // the same file every call; not undo-tracked, not read back by
        // anything automatically. See GraphEditController::
        // exportSnapshotToFile's own doc comment for the full scope.
        options = options.withNativeFunction ("graphExportSnapshot", [&processor] (Args, Completion completion)
        {
            auto& controller = processor.getGraphEditController();
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile ("exported-patch.json");
            const auto result = controller.exportSnapshotToFile (file);

            auto* obj = new juce::DynamicObject();
            obj->setProperty ("success", result.success);
            obj->setProperty ("errorMessage", result.errorMessage);
            obj->setProperty ("path", file.getFullPathName());
            completion (juce::var (obj));
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

        // 09-28-InstanceAllocator arc: a plain debugging read, not a graph-
        // editing command (no NodeGraph mutation, no recompile) — same
        // "own native function, not folded into the snapshot JSON" reasoning
        // getNodeDescriptors already established, since this is derived,
        // recomputed-every-compile state, never part of the persisted
        // PatchDocument the snapshot mirrors. Returns a plain
        // {"nodeId": "voice"|"global"|"mono"} JSON object.
        options = options.withNativeFunction ("graphGetNodeDomains", [&processor] (Args, Completion completion)
        {
            auto& controller = processor.getGraphEditController();
            auto* obj = new juce::DynamicObject();
            for (const auto& [nodeId, domain] : controller.getNodeDomains())
                obj->setProperty (nodeId, domain);
            completion (juce::JSON::toString (juce::var (obj), true));
        });

        // wiki/plans/DomainRedesign.md Batch 4: DomainDot's real
        // replacement — same "plain debugging read" reasoning as
        // graphGetNodeDomains above, which this is meant to eventually
        // replace at the UI layer (graphGetNodeDomains itself stays, still
        // real and tested, since a per-node voice/global/mono label is
        // still meaningful, independent info). Returns
        // { "ports": { nodeId: { portId: { "kind": "poly"|"scalar",
        // "originId"?: string } } },
        //   "badges": { nodeId: { "activeCount": n, "maxCount": n } } } —
        // "badges" entries exist only for "instance.allocate.voice" nodes,
        // and their two numbers are read FRESH off the processor's live
        // atomics on every call (they change on every voice on/off, far
        // more often than a recompile), not cached on the controller.
        options = options.withNativeFunction ("graphGetNodeMultiplicity", [&processor] (Args, Completion completion)
        {
            auto& controller = processor.getGraphEditController();

            auto* portsObj = new juce::DynamicObject();
            for (const auto& [nodeId, ports] : controller.getPortMultiplicity())
            {
                auto* nodeObj = new juce::DynamicObject();
                for (const auto& [portId, info] : ports)
                {
                    auto* portObj = new juce::DynamicObject();
                    portObj->setProperty ("kind", info.kind);
                    if (info.originId.isNotEmpty())
                        portObj->setProperty ("originId", info.originId);
                    nodeObj->setProperty (portId, juce::var (portObj));
                }
                portsObj->setProperty (nodeId, juce::var (nodeObj));
            }

            auto* badgesObj = new juce::DynamicObject();
            for (const auto& [nodeId, bundleIndex] : controller.getOriginBundleIndices())
            {
                auto* badgeObj = new juce::DynamicObject();
                badgeObj->setProperty ("activeCount", processor.getOriginActiveVoiceCount (bundleIndex));
                badgeObj->setProperty ("maxCount", processor.getOriginMaxVoices (bundleIndex));
                badgesObj->setProperty (nodeId, juce::var (badgeObj));
            }

            auto* root = new juce::DynamicObject();
            root->setProperty ("ports", juce::var (portsObj));
            root->setProperty ("badges", juce::var (badgesObj));
            completion (juce::JSON::toString (juce::var (root), true));
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

        // M20 (NODE_EDITOR.md §9): dynamic, viewport-driven preview-tap
        // subscription, keyed by (nodeId, portId) — resolved against
        // whichever plan (global or voice-domain) currently contains that
        // node, not a raw hub-slot claim by an arbitrary string (that lower
        // -level operation never had a real caller — this is the first one).
        // Not a graph-editing command (no recompile involved), so these
        // don't go through GraphEditController. completion(false) means no
        // currently-compiled plan resolves this (nodeId, portId) to a real
        // output buffer, or every voice-domain tap slot is already in use.
        options = options.withNativeFunction ("subscribeNodePreviewTap", [&processor] (Args args, Completion completion)
        {
            const auto kind = previewKindFromString (argString (args, 2));
            completion (processor.subscribeVisualizationTap (argString (args, 0), argString (args, 1), kind));
        });

        options = options.withNativeFunction ("unsubscribeNodePreviewTap", [&processor] (Args args, Completion completion)
        {
            processor.unsubscribeVisualizationTap (argString (args, 0), argString (args, 1));
            completion (true);
        });

        return options;
    }

    std::vector<std::unique_ptr<juce::WebSliderRelay>> BazaltAudioProcessorEditor::makeMacroRelays()
    {
        // "macro1".."macro32" — the JS side binds via getSliderState('macro'
        // + (slot+1)), matching MacroKnob.tsx (wiki/plans/UtilMacro.md).
        std::vector<std::unique_ptr<juce::WebSliderRelay>> relays;
        relays.reserve ((size_t) MacroParameters::numMacros);
        for (int i = 0; i < MacroParameters::numMacros; ++i)
            relays.push_back (std::make_unique<juce::WebSliderRelay> ("macro" + juce::String (i + 1)));
        return relays;
    }

    std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>> BazaltAudioProcessorEditor::makeMacroAttachments (
        BazaltAudioProcessor& processor, std::vector<std::unique_ptr<juce::WebSliderRelay>>& relays)
    {
        std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>> attachments;
        attachments.reserve ((size_t) MacroParameters::numMacros);
        for (int i = 0; i < MacroParameters::numMacros; ++i)
            attachments.push_back (std::make_unique<juce::WebSliderParameterAttachment> (
                processor.getMacroParameter (i), *relays[(size_t) i]));
        return attachments;
    }

    juce::WebBrowserComponent::Options BazaltAudioProcessorEditor::makeWebViewOptions (
        BazaltAudioProcessor& processor, std::vector<std::unique_ptr<juce::WebSliderRelay>>& macroRelays)
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

        for (auto& relay : macroRelays)
            options = options.withOptionsFrom (*relay);

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
          macroRelays (makeMacroRelays()),
          webView (makeWebViewOptions (p, macroRelays)),
          macroAttachments (makeMacroAttachments (p, macroRelays))
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
