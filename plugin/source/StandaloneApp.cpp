/*
    Bazalt's own Standalone application entry point.

    Exists solely to change ONE default. JUCE's stock Standalone app
    (juce_audio_plugin_client_Standalone.cpp -> juce::StandaloneFilterApp)
    hardcodes "mute audio input, to avoid a feedback loop" ON whenever no
    settings file yet has an opinion
    (juce_StandaloneFilterWindow.h's StandalonePluginHolder::
    reloadAudioDeviceState() reads settings->getBoolValue ("shouldMuteInput",
    true) — that "true" is a literal in vendored JUCE, not exposed as an
    option anywhere). That's the checkbox labelled "Feedback Loop: Mute
    audio input" in the audio settings panel.

    Re-muting on every fresh settings file is exactly the kind of thing that
    bites this project in particular: CLAUDE.md's own testing rule is "always
    build+launch the Standalone app" (never a browser), so this checkbox is
    hit constantly, and 2026-09-29's own explicit user instruction was to
    stop defaulting to muted.

    We can't change that literal default without patching vendored JUCE
    (build/_deps/juce-src, re-fetched by CMake — not ours to edit and not
    stable to edit even if we did). JUCE's own sanctioned escape hatch is
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP (see
    juce_audio_plugin_client_Standalone.cpp's own comment on it, and
    plugin/CMakeLists.txt where we set it) — define juce_CreateApplication()
    ourselves. juce::StandaloneFilterApp is declared `final`, so it can't be
    subclassed; everything below except the constructor's last few lines is
    a straight copy of that class.

    This only ever seeds the persisted setting, once, when no settings file
    has recorded a choice yet — the user's own choice, made via the app's
    own "Feedback Loop" checkbox, is read normally afterwards and never
    overwritten here. Purely a changed *default*, not a forced value.
*/

#include <juce_core/system/juce_TargetPlatform.h>

#if JucePlugin_Build_Standalone

#include <juce_audio_plugin_client/detail/juce_IncludeSystemHeaders.h>
#include <juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h>
#include <juce_audio_plugin_client/detail/juce_PluginUtilities.h>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include "BazaltWindowLookAndFeel.h"

namespace bazalt
{

class StandaloneApp final : public juce::JUCEApplication
{
public:
    StandaloneApp()
    {
        juce::PropertiesFile::Options options;

        options.applicationName     = juce::CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName          = "~/.config";
       #else
        options.folderName          = "";
       #endif

        appProperties.setStorageParameters (options);

        // The one behavioural change from stock juce::StandaloneFilterApp -
        // see this file's own top comment.
        if (auto* settings = appProperties.getUserSettings())
            if (! settings->containsKey ("shouldMuteInput"))
                settings->setValue ("shouldMuteInput", false);
    }

    const juce::String getApplicationName() override                 { return juce::CharPointer_UTF8 (JucePlugin_Name); }
    const juce::String getApplicationVersion() override              { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override                       { return true; }
    void anotherInstanceStarted (const juce::String&) override       {}

    juce::StandaloneFilterWindow* createWindow()
    {
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            // No displays are available, so no window will be created!
            jassertfalse;
            return nullptr;
        }

        auto* window = new juce::StandaloneFilterWindow (getApplicationName(),
                                                           juce::LookAndFeel::getDefaultLookAndFeel()
                                                               .findColour (juce::ResizableWindow::backgroundColourId),
                                                           createPluginHolder());

        // Direct instruction: stylize the window frame itself (bg, font) to
        // match ui/'s own theme. Windows' native title bar can't be themed
        // with a custom background colour or font at all (DWM owns that
        // chrome) — the only way to get either is to have JUCE draw the
        // title bar itself. Applied directly to this window instance, not
        // via LookAndFeel::setDefaultLookAndFeel(), so every other native
        // JUCE surface (the audio/MIDI settings dialog, file choosers, …)
        // keeps its stock appearance — see BazaltWindowLookAndFeel's own
        // header comment for the full reasoning. Standalone-app-only: a
        // VST3 instance hosted in a DAW uses the host's own window chrome.
        window->setUsingNativeTitleBar (false);
        window->setLookAndFeel (&windowLookAndFeel);
        // Fixes a real, reported bug, not a style nicety: once the native
        // title bar is off, ResizableWindow reserves a ~4px resize-border
        // margin around the WHOLE window (getBorderThickness(),
        // juce_ResizableWindow.cpp) that neither drawDocumentWindowTitleBar
        // above nor the content component ever paints over — it's filled
        // separately, by ResizableWindow::paint() itself, straight from
        // this colour. Left at JUCE's stock default (unrelated to
        // windowLookAndFeel entirely — it's a per-window property, not a
        // LookAndFeel one), it showed through as an unthemed strip right at
        // the window's true edges — "a weird thick white line separating
        // the new top header with the in app".
        window->setColour (juce::ResizableWindow::backgroundColourId,
                            juce::Colour (BazaltWindowLookAndFeel::backgroundColourArgb));

        return window;
    }

    std::unique_ptr<juce::StandalonePluginHolder> createPluginHolder()
    {
        constexpr auto autoOpenMidiDevices =
       #if (JUCE_ANDROID || JUCE_IOS) && ! JUCE_DONT_AUTO_OPEN_MIDI_DEVICES_ON_MOBILE
                true;
       #else
                false;
       #endif

       #ifdef JucePlugin_PreferredChannelConfigurations
        constexpr juce::StandalonePluginHolder::PluginInOuts channels[] { JucePlugin_PreferredChannelConfigurations };
        const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig (channels, juce::numElementsInArray (channels));
       #else
        const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig;
       #endif

        return std::make_unique<juce::StandalonePluginHolder> (appProperties.getUserSettings(),
                                                                 false,
                                                                 juce::String{},
                                                                 nullptr,
                                                                 channelConfig,
                                                                 autoOpenMidiDevices);
    }

    void initialise (const juce::String&) override
    {
        mainWindow = juce::rawToUniquePtr (createWindow());

        if (mainWindow != nullptr)
        {
           #if JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE
            juce::Desktop::getInstance().setKioskModeComponent (mainWindow.get(), false);
           #endif

            mainWindow->setVisible (true);
        }
        else
        {
            pluginHolder = createPluginHolder();
        }
    }

    void shutdown() override
    {
        pluginHolder = nullptr;
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (pluginHolder != nullptr)
            pluginHolder->savePluginState();

        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay (100, []()
            {
                if (auto app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    juce::ApplicationProperties appProperties;
    // Declared before mainWindow so it outlives it (members are destroyed
    // in reverse declaration order) — mainWindow holds a raw, non-owning
    // pointer to this via Component::setLookAndFeel() in createWindow().
    BazaltWindowLookAndFeel windowLookAndFeel;
    std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
    std::unique_ptr<juce::StandalonePluginHolder> pluginHolder;
};

} // namespace bazalt

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new bazalt::StandaloneApp();
}

#endif // JucePlugin_Build_Standalone
