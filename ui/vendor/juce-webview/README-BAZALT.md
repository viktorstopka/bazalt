This directory is a vendored copy of `@juce-framework/webview`'s **built output**
(`lib/index.js` + type declarations — named `lib/` rather than the upstream
package's `dist/` only so it doesn't collide with `ui/.gitignore`'s blanket
`dist` rule), copied from
`modules/juce_gui_extra/native/typescript/webview-interop/dist/` inside the
exact JUCE `9.0.2` source tree Bazalt pins via CMake `FetchContent` (see
`docs/decisions/0001-juce-version-and-license.md`).

Why vendored instead of `npm install @juce-framework/webview`: the JS-side
protocol here (event names, relay wire format for `WebSliderRelay`/
`WebToggleButtonRelay`/`WebComboBoxRelay`, `getNativeFunction`/`emitEvent`) must
match the C++ side in `build/_deps/juce-src` **exactly** — the public npm package's
version isn't guaranteed to track a specific JUCE tag. Vendoring the copy that
shipped with our own pinned tag removes that whole class of mismatch, the same
way ADR-0001 pins JUCE itself to an exact tag rather than a moving branch.

**Do not hand-edit anything under `lib/`.** To update (only when the JUCE
version pin changes): re-copy `dist/*` (as `lib/*`) and the `LICENSE*`/`README.md` files from
`build/_deps/juce-src/modules/juce_gui_extra/native/typescript/webview-interop/`
after reconfiguring against the new tag, and bump `docs/decisions/0001-juce-
version-and-license.md` in the same change.
