#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace bazalt
{

/** The Standalone app's own window-chrome styling (StandaloneApp.cpp) —
    restyles ONLY the title bar fill/text/buttons to match ui/'s own theme
    (ui/src/theme/tokens.ts: background #0F0F0F, textPrimary, the
    "Schibsted Grotesk" primary font), applied directly to the
    StandaloneFilterWindow instance via Component::setLookAndFeel(), never
    installed as the app-wide default (LookAndFeel::setDefaultLookAndFeel())
    — every other native JUCE surface this app can show (the audio/MIDI
    settings dialog, file choosers, …) keeps stock LookAndFeel_V4 exactly
    as before; only the window frame itself was ever in scope here.

    Subclasses LookAndFeel_V4 (not the base LookAndFeel or V2) specifically
    so everything NOT overridden below keeps V4's modern metrics.

    Only ever meaningful for the Standalone build: a VST3 instance hosted
    in a DAW uses the host's own window chrome, never this one.
*/
class BazaltWindowLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    // tokens.color.background (#0F0F0F) — exposed so StandaloneApp.cpp can
    // also set it as the window's own ResizableWindow::backgroundColourId.
    // That colour (not this LookAndFeel) is what fills the ~4px resize
    // border ResizableWindow reserves around the whole window once native
    // title-bar chrome is off (getBorderThickness(), juce_ResizableWindow.
    // cpp) — neither drawDocumentWindowTitleBar below nor anything else
    // this class overrides ever touches that margin, so leaving the
    // window's own background colour at JUCE's stock default showed
    // through there as a stray, unthemed strip right at the window's
    // true edges — direct feedback, "a weird thick white line separating
    // the new top header with the in app".
    static constexpr juce::uint32 backgroundColourArgb = 0xff0f0f0f;

    BazaltWindowLookAndFeel();

    juce::Button* createDocumentWindowButton (int buttonType) override;

    void drawDocumentWindowTitleBar (juce::DocumentWindow& window, juce::Graphics& g,
                                      int w, int h, int titleSpaceX, int titleSpaceW,
                                      const juce::Image* icon, bool drawTitleTextOnLeft) override;

    // Only ever actually reached by StandaloneFilterWindow's own built-in
    // "Options" TextButton (opens the audio/MIDI settings dialog) — the
    // close/minimise buttons above are a bespoke Button subclass that
    // paints itself directly, never going through this LookAndFeel path.
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

private:
    // Loaded once at construction, kept for the window's whole lifetime —
    // never re-decoded per paint call. The title bar itself no longer
    // draws any text (direct feedback: "remove it", see
    // drawDocumentWindowTitleBar's own comment) — this typeface's one
    // remaining consumer is the "Options" button's own label
    // (getTextButtonFont above), so the whole window frame still reads as
    // one consistent brand, not just its title bar.
    juce::Typeface::Ptr titleTypeface;
};

} // namespace bazalt
