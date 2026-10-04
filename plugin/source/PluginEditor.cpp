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

        // Real, confirmed gap this used to be (wiki/NODES_Gaps.md has the
        // full write-up): a Release build's own resource provider always
        // served `placeholderHtml` above, regardless of whether a real
        // `ui/dist` was ever built — a stale `plugin/CMakeLists.txt` comment
        // claimed "release builds serve ui/dist from disk," but no code
        // anywhere ever actually read it. `findUiDistRoot()`/
        // `serveUiDistFile()` below are that real mechanism, fed by this
        // same CMakeLists.txt's own `CopyUiDist.cmake` post-build step,
        // which ships a built `ui/dist` alongside both the VST3 bundle and
        // the Standalone executable whenever one exists at build time.

        juce::String mimeTypeForExtension (const juce::String& extension) noexcept
        {
            // Every extension ui/dist's own Vite build actually produces
            // (index.html, assets/*.js, assets/*.css, favicon.svg) plus a
            // few common web-asset types a future UI change might add —
            // anything else falls back to a generic binary type, which
            // WebView2 still renders/loads correctly for most purposes.
            if (extension == "html") return "text/html";
            if (extension == "js")   return "application/javascript";
            if (extension == "css")  return "text/css";
            if (extension == "svg")  return "image/svg+xml";
            if (extension == "json") return "application/json";
            if (extension == "png")  return "image/png";
            if (extension == "jpg" || extension == "jpeg") return "image/jpeg";
            if (extension == "woff") return "font/woff";
            if (extension == "woff2") return "font/woff2";
            if (extension == "ico")  return "image/x-icon";
            return "application/octet-stream";
        }

        // A real, built `ui/dist`, shipped alongside this binary by
        // CMakeLists.txt's own post-build copy step — tried in both
        // possible packaging shapes, since a VST3 bundle and a flat
        // Standalone executable sit at different depths relative to their
        // own output directory:
        //   VST3:       Bazalt.vst3/Contents/x86_64-win/Bazalt.vst3 (this
        //               binary) alongside Bazalt.vst3/Contents/Resources/ui/
        //   Standalone: Bazalt.exe alongside a flat sibling ui/ folder
        // Returns an invalid (default-constructed) File if neither exists —
        // the caller falls back to the placeholder in that case, same as
        // always. Computed once (a plugin binary's own on-disk location
        // never changes mid-process) via a function-local static, which is
        // thread-safe initialization (C++11 "magic statics") in case
        // WebView2's resource-provider callback ever runs off the message
        // thread.
        const juce::File& findUiDistRoot()
        {
            static const juce::File uiDistRoot = [] () -> juce::File
            {
                const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

                const auto bundleUi = exe.getParentDirectory().getParentDirectory()
                                          .getChildFile ("Resources").getChildFile ("ui");
                if (bundleUi.getChildFile ("index.html").existsAsFile())
                    return bundleUi;

                const auto siblingUi = exe.getParentDirectory().getChildFile ("ui");
                if (siblingUi.getChildFile ("index.html").existsAsFile())
                    return siblingUi;

                return {};
            }();

            return uiDistRoot;
        }

        std::optional<juce::WebBrowserComponent::Resource> serveUiDistFile (const juce::File& uiDistRoot, const juce::String& url)
        {
            const auto relativePath = (url == "/" || url.isEmpty()) ? juce::String ("index.html")
                                                                      : url.fromFirstOccurrenceOf ("/", false, false);
            const auto file = uiDistRoot.getChildFile (relativePath);

            // Defensive: a resource provider is handed raw request paths, so
            // guard against one that (deliberately or not) tries to escape
            // uiDistRoot via "../" — this WebView only ever navigates to
            // paths this same binary generates, but there's no cost to
            // checking anyway.
            if (! file.getFullPathName().startsWith (uiDistRoot.getFullPathName()) || ! file.existsAsFile())
                return std::nullopt;

            juce::MemoryBlock block;
            if (! file.loadFileAsData (block))
                return std::nullopt;

            const auto* begin = reinterpret_cast<const std::byte*> (block.getData());
            const auto* end = begin + block.getSize();

            return juce::WebBrowserComponent::Resource {
                std::vector<std::byte> (begin, end),
                mimeTypeForExtension (file.getFileExtension().trimCharactersAtStart ("."))
            };
        }

        std::optional<bazalt::engine::TelemetryFrameType> frameTypeFromPathSegment (const juce::String& segment)
        {
            if (segment == "scope")
                return bazalt::engine::TelemetryFrameType::Oscilloscope;
            if (segment == "spectrum")
                return bazalt::engine::TelemetryFrameType::Spectrum;
            if (segment == "meter")
                return bazalt::engine::TelemetryFrameType::Meter;
            if (segment == "eventImpulse")
                return bazalt::engine::TelemetryFrameType::EventImpulse;
            if (segment == "history")
                return bazalt::engine::TelemetryFrameType::RollingHistory;
            if (segment == "phaseLocked")
                return bazalt::engine::TelemetryFrameType::PhaseLocked;
            return std::nullopt;
        }

        // ARCHITECTURE.md §6.3: "bazalt-tap://scope/main" is the
        // conceptual shape; JUCE's resource provider is one callback per
        // WebBrowserComponent differentiated by PATH within a single
        // virtual origin, not a literal custom URL scheme — so taps are
        // served at /tap/<name>/<scope|spectrum|meter|eventImpulse|history>
        // instead.
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

            const auto& uiDistRoot = findUiDistRoot();
            if (uiDistRoot != juce::File())
            {
                if (auto resource = serveUiDistFile (uiDistRoot, url))
                    return resource;

                // A real ui/dist is shipped but this specific path wasn't
                // found in it (e.g. a damaged copy, or a genuinely unknown
                // request) — fall through to the placeholder below only for
                // "/"/"/index.html" so the app still shows SOMETHING rather
                // than a blank page; any other unknown path stays a real
                // 404 (std::nullopt), same as before this change.
            }

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

        // wiki/plans/UtilMacro.md P2.1 (post-ship sweep): one call that adds
        // a fully-configured util.macro node (id, x, y, slot, min, max,
        // isInteger, quantity, unit, value) in a single recompile, replacing
        // what used to be graphAddNode + 5x graphSetParameterValue +
        // graphSetProperty (6 separate recompiles) for the same gesture. The
        // caller still follows up with graphConnectWithAutoAdapt separately
        // to wire it in — see GraphEditController::createMacro's own doc
        // comment for why that step isn't folded in here too.
        options = options.withNativeFunction ("graphCreateMacro", [&processor] (Args args, Completion completion)
        {
            auto& controller = processor.getGraphEditController();
            const auto result = controller.createMacro (argString (args, 0), argFloat (args, 1), argFloat (args, 2),
                                                           (int) argFloat (args, 3), argFloat (args, 4), argFloat (args, 5),
                                                           argFloat (args, 6) >= 0.5f, (int) argFloat (args, 7),
                                                           argString (args, 8), argFloat (args, 9));
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
            // A commit ends any live drag of the same value (LiveParameterEdits.h):
            // its slot glides to the final value and frees itself.
            processor.getLiveParameterEdits().release (argString (args, 0), argString (args, 1));
            completion (commandResultToVar (result));
        });

        // While a slider is dragged: stream the value to the running nodes,
        // smoothed, without recompiling (LiveParameterEdits.h). Only values a
        // running node can take live — a port's fallback value or a
        // non-structural parameter; anything else waits for the commit.
        options = options.withNativeFunction ("graphSetParameterLive", [&processor] (Args args, Completion completion)
        {
            const auto nodeId = argString (args, 0);
            const auto parameterId = argString (args, 1);
            auto& live = processor.getLiveParameterEdits();
            const auto ok = (live.isActive (nodeId, parameterId) || processor.getGraphEditController().isLiveEditable (nodeId, parameterId))
                            && live.set (nodeId, parameterId, argFloat (args, 2));
            completion (ok);
        });

        // The drag ended (with or without a commit): the slot glides to its
        // last value and frees itself.
        options = options.withNativeFunction ("graphReleaseParameterLive", [&processor] (Args args, Completion completion)
        {
            processor.getLiveParameterEdits().release (argString (args, 0), argString (args, 1));
            completion (true);
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

        // A real, DAW-relevant gap this closes (see
        // BazaltAudioProcessor::getInstanceId()'s own doc comment): every
        // instance used to point at the SAME fixed user-data folder —
        // never exercised by the Standalone app (always exactly one
        // instance), but a real host routinely runs several instances of
        // the same plugin at once, and the Standalone app could easily be
        // running alongside a DAW's own instance as two unrelated
        // processes pointing at the same folder either way.
        const auto webView2UserDataFolder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                 .getChildFile ("Bazalt")
                                                 .getChildFile ("WebView2")
                                                 .getChildFile (processor.getInstanceId().toString());

        auto options = Options {}
                            .withBackend (Options::Backend::webview2)
                            .withWinWebView2Options (
                                Options::WinWebView2 {}
                                    .withUserDataFolder (webView2UserDataFolder)
                                    // Direct feedback: "a weird thick white line separating
                                    // the new top header with the in app". Root cause: this
                                    // option was never set, so WebView2's own
                                    // DefaultBackgroundColor stayed at its default — fully
                                    // TRANSPARENT (a default-constructed juce::Colour, alpha
                                    // 0) — "underneath all web content" per this option's own
                                    // doc comment. WebView2 is a genuinely separate native
                                    // child HWND, composited by Windows, not painted through
                                    // JUCE's own Graphics/LookAndFeel at all — setting the
                                    // window's own ResizableWindow::backgroundColourId
                                    // (StandaloneApp.cpp) could never reach this seam, because
                                    // transparent, non-layered child-HWND compositing isn't
                                    // pixel-perfect at every DPI-scaled edge, and whatever
                                    // shows through at that seam is NOT guaranteed to be this
                                    // window's own background. Matches tokens.color.background
                                    // exactly and must be fully opaque (see this option's own
                                    // assertion) — ui/'s own CSS (index.css's `body { background:
                                    // var(--color-background) }`) covers the rest once the page
                                    // itself has painted; this is what's visible in the moments
                                    // and edges that page content doesn't.
                                    .withBackgroundColour (juce::Colour (0xff0f0f0f)))
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
