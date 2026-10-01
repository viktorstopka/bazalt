# Disables WebView2's own native Ctrl+MouseWheel/Ctrl+Plus/Ctrl+Minus/pinch
# zoom in JUCE's Windows WebBrowserComponent backend — see
# cmake/patches/0001-webview2-disable-native-zoom.patch for the full
# reasoning (that file is the human-readable record of exactly what this
# changes, in standard diff form; THIS script is what actually applies it —
# not `git apply`/`patch`, both of which are sensitive to the CRLF
# line-ending differences a fresh Windows git checkout of JUCE can
# introduce, for a change this small and well-known).
#
# Run once, as FetchContent's PATCH_COMMAND, right after JUCE is first
# fetched (never re-run on a configure where `build/_deps/juce-src` already
# exists — FetchContent's own documented behaviour). Idempotent regardless
# (checks the marker string first) so a cache that's somehow already patched
# is a silent no-op, never a second, broken edit.
#
# PATCH_COMMAND's own working directory is the fetched source root
# (ExternalProject_Add's documented contract, which FetchContent_Declare's
# PATCH_COMMAND forwards to) — this uses a path relative to that, never an
# absolute one baked in at configure time.
set(_bazalt_webview2_file "modules/juce_gui_extra/native/juce_WebBrowserComponent_windows.cpp")

file(READ "${_bazalt_webview2_file}" _bazalt_webview2_contents)

string(FIND "${_bazalt_webview2_contents}" "put_IsZoomControlEnabled" _bazalt_webview2_already_patched)

if(_bazalt_webview2_already_patched EQUAL -1)
    set(_bazalt_webview2_anchor "settings->put_IsBuiltInErrorPageEnabled (! preferences.getWinWebView2BackendOptions().getIsBuiltInErrorPageDisabled());")
    set(_bazalt_webview2_replacement
"${_bazalt_webview2_anchor}

            // Bazalt patch (see cmake/patches/0001-webview2-disable-native-zoom.patch):
            // disables WebView2's own native Ctrl+MouseWheel/Ctrl+Plus/Ctrl+Minus/
            // pinch zoom, which operates at the WebView2 host level, entirely
            // independent of the hosted page's own wheel-event handling (the
            // page's preventDefault() on a wheel event does NOT suppress it -
            // Microsoft's own documented behaviour for IsZoomControlEnabled).
            // Bazalt's own node-editor canvas (InfiniteCanvas.tsx) implements its
            // own pan/zoom entirely in JS/CSS against camera.zoom - a second,
            // independent native zoom layered on top of that is never wanted,
            // and was the real root cause behind a real, repeatedly-reported bug
            // (content cropped/offset at odd zoom levels, borders and eventually
            // everything disappearing above a zoom threshold - two independent
            // zoom transforms compounding, not one implementation's own bug).
            settings->put_IsZoomControlEnabled (false);")

    string(REPLACE "${_bazalt_webview2_anchor}" "${_bazalt_webview2_replacement}"
        _bazalt_webview2_contents "${_bazalt_webview2_contents}")

    file(WRITE "${_bazalt_webview2_file}" "${_bazalt_webview2_contents}")
    message(STATUS "Bazalt: patched JUCE's WebView2 backend to disable native zoom")
else()
    message(STATUS "Bazalt: JUCE WebView2 native-zoom patch already applied")
endif()
