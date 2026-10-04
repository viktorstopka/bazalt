#include "BazaltWindowLookAndFeel.h"
#include "BinaryData.h"

namespace bazalt
{

namespace
{
    // ui/src/theme/tokens.ts is the single source of truth for these —
    // kept as a literal copy here rather than parsed from that file at
    // build time (no existing mechanism shares design tokens across the
    // plugin <-> ui language boundary). Update both places by hand if
    // these ever change. backgroundColourArgb itself is the header's own
    // public static constexpr, not redeclared here.
    constexpr juce::uint32 textColourArgb = 0xffe8e8ea; // tokens.color.textPrimary
    // The only accent colour in this file — a plain, muted red hinting
    // "close" on hover, not a filled traffic-light dot: no filled circular
    // button backgrounds anywhere else in this app's own aesthetic
    // (NodeCard.css's own "no glow/box-shadow... reads as an
    // AI-generated-UI cliché" rule applies here too).
    constexpr juce::uint32 closeHoverColourArgb = 0xffcc4444;

    /** A single minimal title-bar button (close/minimise/maximise): a thin
        stroked glyph on a flat, borderless background, styled to match
        this app's own controls (NodeCard.css: "no hover glow... flat
        colour, same weight as the resting state") rather than JUCE's
        stock LookAndFeel_V4_DocumentWindowButton's filled traffic-light
        dots.
    */
    class TitleBarButton final : public juce::Button
    {
    public:
        TitleBarButton (const juce::String& name, juce::Path shapeToUse, bool isCloseButtonToUse)
            : juce::Button (name), shape (std::move (shapeToUse)), isCloseButton (isCloseButtonToUse)
        {
        }

        void paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
        {
            if (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown)
            {
                auto hoverColour = isCloseButton ? juce::Colour (closeHoverColourArgb)
                                                  : juce::Colour (textColourArgb).withAlpha (0.14f);
                g.setColour (shouldDrawButtonAsDown ? hoverColour.withAlpha (isCloseButton ? 1.0f : 0.22f) : hoverColour);
                g.fillAll();
            }

            auto glyphIsOnColouredFill = isCloseButton && (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown);
            g.setColour (glyphIsOnColouredFill ? juce::Colours::white : juce::Colour (textColourArgb).withAlpha (0.8f));

            auto reduced = getLocalBounds().toFloat().reduced ((float) getHeight() * 0.34f);
            g.strokePath (shape, juce::PathStrokeType (1.4f), shape.getTransformToScaleToFit (reduced, true));
        }

    private:
        juce::Path shape;
        bool isCloseButton;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TitleBarButton)
    };
} // namespace

BazaltWindowLookAndFeel::BazaltWindowLookAndFeel()
{
    titleTypeface = juce::Typeface::createSystemTypefaceFor (BinaryData::SchibstedGroteskBold_ttf,
                                                               (size_t) BinaryData::SchibstedGroteskBold_ttfSize);
}

juce::Button* BazaltWindowLookAndFeel::createDocumentWindowButton (int buttonType)
{
    juce::Path shape;

    if (buttonType == juce::DocumentWindow::closeButton)
    {
        shape.addLineSegment ({ 0.0f, 0.0f, 1.0f, 1.0f }, 0.0f);
        shape.addLineSegment ({ 1.0f, 0.0f, 0.0f, 1.0f }, 0.0f);
        return new TitleBarButton ("close", shape, true);
    }

    if (buttonType == juce::DocumentWindow::minimiseButton)
    {
        shape.addLineSegment ({ 0.0f, 0.5f, 1.0f, 0.5f }, 0.0f);
        return new TitleBarButton ("minimise", shape, false);
    }

    if (buttonType == juce::DocumentWindow::maximiseButton)
    {
        // Not requested by StandaloneFilterWindow's own button flags today
        // (minimiseButton | closeButton only) — implemented anyway so this
        // LookAndFeel degrades sensibly rather than returning nullptr if
        // that ever changes.
        shape.addRectangle (0.1f, 0.1f, 0.8f, 0.8f);
        return new TitleBarButton ("maximise", shape, false);
    }

    jassertfalse;
    return nullptr;
}

void BazaltWindowLookAndFeel::drawDocumentWindowTitleBar (juce::DocumentWindow& window, juce::Graphics& g,
                                                           int w, int h, int titleSpaceX, int titleSpaceW,
                                                           const juce::Image* icon, bool drawTitleTextOnLeft)
{
    // Direct feedback: "Instead of having the custom bazalt text in the
    // uppermost header, remove it" — the in-app top bar (App.css's own
    // .top-bar-title) already shows the Bazalt name; this native title bar
    // is now just a plain coloured strip for the window controls, no text.
    juce::ignoreUnused (window, titleSpaceX, titleSpaceW, icon, drawTitleTextOnLeft);

    if (w * h == 0)
        return;

    g.setColour (juce::Colour (backgroundColourArgb));
    g.fillAll();
}

void BazaltWindowLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                                     bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    // "The options button should match with its style. Background. and the
    // rounded corner should be only ever so slight rounding" — direct
    // feedback on StandaloneFilterWindow's own built-in "Options" button
    // (opens the audio/MIDI settings dialog), which until now fell through
    // to LookAndFeel_V4's stock TextButton look (its own dark-blue-grey
    // ColourScheme::widgetBackground, never touched by this class) rather
    // than this window's own background.
    if (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown)
    {
        g.setColour (juce::Colour (textColourArgb).withAlpha (shouldDrawButtonAsDown ? 0.22f : 0.14f));
        g.fillRoundedRectangle (button.getLocalBounds().toFloat(), 4.0f); // ui/src/theme/tokens.ts's own --radius-sm
        return;
    }

    g.setColour (juce::Colour (backgroundColourArgb));
    g.fillRoundedRectangle (button.getLocalBounds().toFloat(), 4.0f);
}

juce::Font BazaltWindowLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    // The title bar no longer draws any text of its own (see
    // drawDocumentWindowTitleBar above) — the Options button's label is
    // this typeface's one remaining use, so the window frame still reads
    // as one consistent brand rather than just its title bar having been
    // on-brand briefly. Same height formula LookAndFeel_V4's own default
    // getTextButtonFont uses, just this typeface instead of the stock one.
    auto fontOptions = titleTypeface != nullptr
                            ? juce::FontOptions (titleTypeface).withHeight (juce::jmin (16.0f, (float) buttonHeight * 0.6f))
                            : juce::FontOptions (juce::jmin (16.0f, (float) buttonHeight * 0.6f));
    return juce::Font (fontOptions);
}

} // namespace bazalt
