# 0032 — Disable WebView2's own native zoom; patch vendored JUCE, don't try to catch it from JS/CSS

## Status
Accepted and implemented, 2026-10-01. The first time this project patches a vendored dependency
rather than its own code — see Context for why that was the right call here, not a precedent to
reach for lightly elsewhere.

## Context
Direct feedback, after the node editor's own camera (pan/zoom) had already shipped and been
exercised for a while: "it now zooms weird as hell... zooms independently from the frame...
too big for the frame so is cropped... on zoom, sometimes some borders disappear, and after
zooming above a threshold everything just disappears." `InfiniteCanvas.css`'s own header comment
had already named the suspect, as a known, accepted limitation, before this was ever reported as
a live bug: "WebView-ness... WebView2's own native pinch-zoom/edge-swipe handling isn't reachable
from CSS/JS at all; that would need a JUCE-side `WebBrowserComponent` option."

Confirmed directly against JUCE's own vendored source (`build/_deps/juce-src`) rather than left as
a guess: `WebBrowserComponent::Options::WinWebView2` exposes no zoom-related option at all (only
`withDLLLocation`/`withUserDataFolder`/`withStatusBarDisabled`/`withBuiltInErrorPageDisabled`/
`withBackgroundColour`) — contrast the Linux WebKit backend's own `LinuxWkWebView::
withNativeZoomGesture`, which is **disabled by default** there specifically so pinch/Ctrl+wheel
translate into ordinary JS wheel events instead of native browser zoom. No equivalent exists for
Windows. `juce_WebBrowserComponent_windows.cpp`'s own `setWebViewPreferences()` already creates an
`ICoreWebView2Settings` instance and calls a handful of `put_*` methods on it
(`put_IsStatusBarEnabled`, `put_IsBuiltInErrorPageEnabled`, ...) but never touches
`put_IsZoomControlEnabled` — WebView2's default for that property is enabled, and per Microsoft's
own documentation it governs Ctrl+MouseWheel/Ctrl+Plus/Ctrl+Minus/pinch zoom at the WebView2 HOST
level, independent of whatever the hosted page's own JS does with a `wheel` event's
`preventDefault()` (already called unconditionally by `InfiniteCanvas.tsx`'s own `onWheel` handler
— confirmed this was never a missing-`preventDefault()` bug on the app side).

That's the real mechanism behind every symptom reported: two independent zoom transforms were
compounding — the app's own `camera.zoom` (JS/CSS, clamped to `[0.1, 8]`, the only one
`InfiniteCanvas.tsx`'s pan/zoom code has ever known about) and WebView2's own native host-level
zoom (uncontrolled, triggerable by the same Ctrl+wheel gesture the app's own zoom already
responds to), neither aware of the other. "Too big for the frame, cropped" is native zoom scaling
the whole rendered page past the WebView2 control's own viewport, which clips it at the HOST level
— a different clipping boundary than the app's own `.infinite-canvas { overflow: hidden }`, which
only ever expected its own `camera.zoom` to need accounting for. "Borders disappearing" and
"everything disappearing above a threshold" are native zoom pushing the COMBINED effective scale
(camera.zoom × WebView2's own factor) into territory neither transform was ever designed or tested
against on its own.

No public JUCE API reaches `ICoreWebView2Settings` from outside the class (`WebBrowserComponent`
exposes no native-handle escape hatch at all — checked the full public header, not assumed), so
the only way to actually call `put_IsZoomControlEnabled(FALSE)` is inside JUCE's own Windows
backend file.

## Decision
Patch `juce_WebBrowserComponent_windows.cpp` (`setWebViewPreferences()`) to call
`settings->put_IsZoomControlEnabled (false)`, right alongside its existing `put_IsStatusBarEnabled`/
`put_IsBuiltInErrorPageEnabled` calls — same interface, same pattern, one line. Applied via
`FetchContent_Declare(JUCE ...)`'s own `PATCH_COMMAND` (`cmake/ApplyJuceWebView2ZoomPatch.cmake`)
rather than hand-editing `build/_deps/juce-src` and leaving it there: `build/_deps` is a fetched
cache, silently wiped and re-populated plain on a clean rebuild or a fresh clone, and a fix that
only exists until the next `rm -rf build` is not a real fix. `PATCH_COMMAND` runs exactly once,
right after JUCE is first fetched (FetchContent's own documented behaviour — never re-run against
an already-populated `juce-src`), so this survives indefinitely without needing to re-apply by
hand. The script does a plain, idempotent `file(READ)`/`string(REPLACE)`/`file(WRITE)` rather than
`git apply`/the `patch` tool against a committed `.patch` file — both are sensitive to the CRLF
line-ending differences a fresh Windows git checkout of JUCE can introduce, not worth the fragility
for a change this small and well-known; `cmake/patches/0001-webview2-disable-native-zoom.patch`
still exists as the human-readable record of exactly what changes, in standard diff form — it's
documentation, not the executed mechanism.

This is a real, deliberate exception to "don't hand-edit a vendored dependency" (the same rule
`ui/vendor/juce-webview/README-BAZALT.md` states for a different vendored piece) — justified here
specifically because (a) JUCE's own public API genuinely has no other way to reach this setting,
confirmed by reading it, not assumed, and (b) the patch targets one named, well-understood native
WebView2 property with a stable public meaning (not vendored JUCE *behavior* this project might
want to diverge from on principle) — not a precedent for patching JUCE whenever a public API would
be merely inconvenient to work around.

## Consequences
- Bazalt's own node-editor camera (`InfiniteCanvas.tsx`) is now the *only* zoom mechanism active
  inside the WebView, on every platform this project currently ships (Windows x64 only through
  M6, CLAUDE.md rule 7) — no second, independent transform ever compounds with it again.
- `InfiniteCanvas.css`'s own header comment ("not reachable from CSS/JS... would need a JUCE-side
  option") is now out of date in the specific sense that the JUCE-side option has been built — the
  comment's broader point (CSS/JS alone genuinely can't reach this) stays true and is exactly why a
  native-side patch was the right call, not a JS workaround.
- A future JUCE version bump (`GIT_TAG` in the top-level `CMakeLists.txt`) needs this patch
  re-verified against whatever `setWebViewPreferences()` looks like in the new tag — the anchor
  text `ApplyJuceWebView2ZoomPatch.cmake` matches against is a literal string, not a line number,
  so a reformatted (not just relocated) call site would need the script's own anchor updated too.
  Flagged here so a future JUCE bump doesn't silently lose this fix.
- No equivalent patch exists for macOS/Linux WebView backends — out of scope per CLAUDE.md rule 7
  (Windows x64 only through M6); Linux's own WebKit backend already defaults to the JS-visible
  behaviour this ADR achieves for Windows (see Context), so nothing there needs this at all.
