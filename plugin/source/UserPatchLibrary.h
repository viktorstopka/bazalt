#pragma once

#include "bazalt/engine/patch/PatchSerializer.h"
#include <juce_core/juce_core.h>

namespace bazalt
{
    /** The user's own saved patches — one `.bazalt` file (ordinary PatchDocument
        JSON) per patch, in the per-user application-data folder:
          Windows  %APPDATA%\Bazalt\Patches
          macOS    ~/Library/Bazalt/Patches
          Linux    ~/.config/Bazalt/Patches
        A plugin's working directory is the DAW's and is often not writable,
        so the plugin (VST3) and the Standalone both use this same folder —
        a patch saved in one shows up in the other.

        Factory patches are not here: they ship inside the UI
        (ui/src/PatchMenu.tsx). The menu labels each list "Factory" / "You" by
        where it came from; nothing in a file claims to be a factory patch.

        Message thread only (the WebView's native-function handlers).
    */
    class UserPatchLibrary
    {
    public:
        static constexpr const char* fileExtension = ".bazalt";

        struct Entry
        {
            juce::String name;     // the patch's own meta.name (falls back to the file name)
            juce::String fileName; // the stable handle the UI loads/deletes by
            juce::int64 modifiedAtMs = 0;
        };

        explicit UserPatchLibrary (juce::File directoryToUse = defaultDirectory()) : directory (std::move (directoryToUse)) {}

        static juce::File defaultDirectory()
        {
            return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Bazalt").getChildFile ("Patches");
        }

        const juce::File& getDirectory() const noexcept { return directory; }

        /** Every saved patch, sorted by name (case-insensitive). */
        juce::Array<Entry> list() const
        {
            juce::Array<Entry> entries;
            for (const auto& file : directory.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension))
            {
                Entry entry;
                entry.fileName = file.getFileName();
                entry.modifiedAtMs = file.getLastModificationTime().toMilliseconds();
                const auto parsed = bazalt::engine::parsePatchFromJson (file.loadFileAsString());
                entry.name = parsed.success && parsed.document.meta.name.isNotEmpty() ? parsed.document.meta.name
                                                                                      : file.getFileNameWithoutExtension();
                entries.add (entry);
            }
            std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b) { return a.name.compareIgnoreCase (b.name) < 0; });
            return entries;
        }

        /** The file a patch called `name` is saved to. Two names that only
            differ in characters a file name can't hold map to the same file —
            saving the second overwrites the first, which the UI confirms. */
        juce::File fileFor (const juce::String& name) const
        {
            auto stem = juce::File::createLegalFileName (name.trim());
            if (stem.isEmpty())
                stem = "Untitled";
            return directory.getChildFile (stem + fileExtension);
        }

        bool exists (const juce::String& name) const { return fileFor (name).existsAsFile(); }

        /** Saves `document` as `name` (stamping meta.name, keeping the
            original creation time when overwriting). Returns the file name,
            or an empty string on failure with `error` set. */
        juce::String save (const juce::String& name, bazalt::engine::PatchDocument document, juce::String& error) const
        {
            const auto trimmed = name.trim();
            if (trimmed.isEmpty())
            {
                error = "A patch needs a name";
                return {};
            }
            if (! directory.exists() && ! directory.createDirectory())
            {
                error = "Could not create " + directory.getFullPathName();
                return {};
            }

            const auto file = fileFor (trimmed);
            const auto now = juce::Time::currentTimeMillis();
            document.meta.name = trimmed;
            document.meta.modifiedAtMs = now;
            document.meta.createdAtMs = now;
            if (file.existsAsFile())
                if (const auto previous = bazalt::engine::parsePatchFromJson (file.loadFileAsString()); previous.success && previous.document.meta.createdAtMs > 0)
                    document.meta.createdAtMs = previous.document.meta.createdAtMs;

            if (! file.replaceWithText (bazalt::engine::serializePatchToJson (document, true)))
            {
                error = "Could not write " + file.getFullPathName();
                return {};
            }
            return file.getFileName();
        }

        /** The patch's JSON, or an empty string if there is no such file.
            Only names inside the library directory are accepted. */
        juce::String load (const juce::String& fileName) const
        {
            const auto file = resolve (fileName);
            return file.existsAsFile() ? file.loadFileAsString() : juce::String();
        }

        bool remove (const juce::String& fileName) const
        {
            const auto file = resolve (fileName);
            return file.existsAsFile() && file.deleteFile();
        }

    private:
        /** A bare file name from the UI, never a path: anything with a
            separator or a parent reference resolves to nothing. */
        juce::File resolve (const juce::String& fileName) const
        {
            if (fileName.isEmpty() || fileName.containsAnyOf ("/\\") || fileName.contains ("..") || ! fileName.endsWith (fileExtension))
                return {};
            return directory.getChildFile (fileName);
        }

        juce::File directory;
    };
}
