# Copies the built `ui/dist` (`cd ui && npm run build`) into a plugin
# format's own build output, so a Release VST3/Standalone binary is
# genuinely self-contained — doesn't need Node/npm or a dev server on the
# machine it's finally run on. This closes a real, confirmed gap: the
# Release-build resource provider (`PluginEditor.cpp`'s `serveResource()`)
# used to always serve a hardcoded placeholder page regardless of whether
# `ui/dist` existed at all — a stale comment in `plugin/CMakeLists.txt`
# claimed otherwise, but the actual code never read `ui/dist` from disk
# until this script + `findUiDistRoot()` landed together.
#
# SRC/DST are passed in as plain, already generator-expression-resolved
# strings (see `plugin/CMakeLists.txt`'s own `add_custom_command` calls).
# A missing `ui/dist` is NOT a build failure — it's a separately-built,
# optional artifact (most Debug iteration never touches it, since Debug
# loads the live Vite dev server instead, `PluginEditor.cpp`'s own
# `JUCE_DEBUG` branch) — just a status message, so it's never a silent
# surprise either.
if(NOT EXISTS "${SRC}/index.html")
    message(STATUS "Bazalt: ui/dist not built (missing ${SRC}/index.html) - skipping UI asset copy for this target. Run 'npm run build' in ui/ first if you need a self-contained Release build.")
    return()
endif()

# `cmake -E copy_directory <src> <dst>` copies SRC's own CONTENTS into DST
# (creating DST if needed) — the semantics this needs; `file(COPY ...)`'s
# own CMake-script equivalent would nest an extra "dist" directory level
# instead, which `findUiDistRoot()` doesn't expect.
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory "${SRC}" "${DST}")
message(STATUS "Bazalt: copied ui/dist into ${DST}")
