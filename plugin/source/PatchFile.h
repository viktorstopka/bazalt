#pragma once

#include "SampleLibrary.h"
#include "bazalt/engine/patch/ExactFloat.h"
#include <juce_core/juce_core.h>

namespace bazalt
{
    /** The `.bazalt` file (wiki/plans/Release.md R0.1b): one file you can send,
        never a folder. It is a zip holding
          patch.json         the patch — PatchSerializer's JSON, readable
          assets/<id>.<ext>  each asset's bytes as they are: samples as FLAC,
                             images as PNG/JPEG — no base64
        `patch.json`'s `assets` entries name their file ({"type", "file"}) instead
        of carrying base64 `data`. Assets are stored without recompression (they
        are compressed already); only patch.json is deflated.

        Inside the app a patch is still the plain JSON string with base64 asset
        data (what the UI, undo snapshots and PatchSerializer use): `read` turns a
        file into that, `write` turns that into a file. `read` also accepts a
        plain-JSON patch (a readable export, factory content in the repo).

        Message thread (patch library, export, load). */
    namespace PatchFile
    {
        constexpr const char* documentEntry = "patch.json";
        constexpr const char* assetsFolder = "assets/";

        inline juce::String extensionFor (const juce::String& mimeType)
        {
            if (mimeType == "audio/flac") return "flac";
            if (mimeType == "image/png") return "png";
            if (mimeType == "image/jpeg") return "jpg";
            if (mimeType == "image/webp") return "webp";
            if (mimeType == "image/gif") return "gif";
            return "bin";
        }

        /** An asset id as a file name: "sample:1a2b" -> "sample-1a2b". */
        inline juce::String fileNameFor (const juce::String& assetId, const juce::String& mimeType)
        {
            return assetsFolder + juce::File::createLegalFileName (assetId.replaceCharacter (':', '-')) + "." + extensionFor (mimeType);
        }

        inline bool isZip (const juce::MemoryBlock& bytes)
        {
            return bytes.getSize() >= 4 && static_cast<const char*> (bytes.getData())[0] == 'P' && static_cast<const char*> (bytes.getData())[1] == 'K';
        }

        /** A patch (PatchSerializer JSON, base64 assets) as `.bazalt` bytes.
            False when the JSON isn't a patch or an asset isn't valid base64. */
        inline bool write (const juce::String& patchJson, juce::MemoryBlock& out, juce::String& error)
        {
            auto root = juce::JSON::parse (patchJson);
            if (! root.isObject())
            {
                error = "Not a patch";
                return false;
            }

            juce::ZipFile::Builder zip;
            juce::OwnedArray<juce::MemoryBlock> keepAlive; // the builder reads its streams when it writes
            if (auto* assets = root["assets"].getDynamicObject())
            {
                auto* rewritten = new juce::DynamicObject();
                for (const auto& property : assets->getProperties())
                {
                    const auto id = property.name.toString();
                    const auto type = property.value["type"].toString();
                    auto* bytes = keepAlive.add (new juce::MemoryBlock());
                    if (! decodeBase64 (property.value["data"].toString(), *bytes))
                    {
                        error = "The asset '" + id + "' isn't readable";
                        return false;
                    }
                    const auto file = fileNameFor (id, type);
                    zip.addEntry (new juce::MemoryInputStream (*bytes, false), 0, file, juce::Time::getCurrentTime());
                    auto* entry = new juce::DynamicObject();
                    entry->setProperty ("type", type);
                    entry->setProperty ("file", file);
                    rewritten->setProperty (property.name, juce::var (entry));
                }
                root.getDynamicObject()->setProperty ("assets", juce::var (rewritten));
            }

            auto json = std::make_unique<juce::MemoryBlock>();
            const auto text = bazalt::engine::toJson (root, false);
            json->append (text.toRawUTF8(), text.getNumBytesAsUTF8());
            zip.addEntry (new juce::MemoryInputStream (*json, false), 6, documentEntry, juce::Time::getCurrentTime());

            out.reset();
            juce::MemoryOutputStream stream (out, false);
            if (! zip.writeToStream (stream, nullptr))
            {
                error = "Could not write the patch file";
                return false;
            }
            stream.flush();
            return true;
        }

        /** `.bazalt` bytes (a container, or a plain-JSON patch) as the patch JSON
            the app uses — each asset's bytes back in as base64 `data`. */
        inline bool read (const juce::MemoryBlock& bytes, juce::String& patchJson, juce::String& error)
        {
            if (! isZip (bytes))
            {
                patchJson = juce::String::fromUTF8 (static_cast<const char*> (bytes.getData()), (int) bytes.getSize());
                if (! patchJson.trimStart().startsWithChar ('{'))
                {
                    error = "Not a Bazalt patch";
                    return false;
                }
                return true;
            }

            juce::ZipFile zip (new juce::MemoryInputStream (bytes, false), true);
            const auto readEntry = [&zip] (const juce::String& name, juce::MemoryBlock& into)
            {
                const auto index = zip.getIndexOfFileName (name);
                if (index < 0)
                    return false;
                std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (index));
                if (stream == nullptr)
                    return false;
                into.reset();
                stream->readIntoMemoryBlock (into);
                return true;
            };

            juce::MemoryBlock document;
            if (! readEntry (documentEntry, document))
            {
                error = "The patch file has no patch in it";
                return false;
            }
            auto root = juce::JSON::parse (document.toString());
            if (! root.isObject())
            {
                error = "The patch file's patch isn't readable";
                return false;
            }

            if (auto* assets = root["assets"].getDynamicObject())
            {
                auto* restored = new juce::DynamicObject();
                for (const auto& property : assets->getProperties())
                {
                    const auto file = property.value["file"].toString();
                    juce::MemoryBlock asset;
                    if (file.isEmpty() || ! readEntry (file, asset))
                    {
                        error = "The patch file is missing '" + file + "'";
                        return false;
                    }
                    auto* entry = new juce::DynamicObject();
                    entry->setProperty ("type", property.value["type"]);
                    entry->setProperty ("data", encodeBase64 (asset));
                    restored->setProperty (property.name, juce::var (entry));
                }
                root.getDynamicObject()->setProperty ("assets", juce::var (restored));
            }
            patchJson = bazalt::engine::toJson (root, false);
            return true;
        }

        inline bool writeFile (const juce::File& file, const juce::String& patchJson, juce::String& error)
        {
            juce::MemoryBlock bytes;
            if (! write (patchJson, bytes, error))
                return false;
            const auto temp = file.getSiblingFile (file.getFileName() + ".part");
            if (! temp.replaceWithData (bytes.getData(), bytes.getSize()) || ! temp.moveFileTo (file))
            {
                temp.deleteFile();
                error = "Could not write " + file.getFullPathName();
                return false;
            }
            return true;
        }

        inline bool readFile (const juce::File& file, juce::String& patchJson, juce::String& error)
        {
            juce::MemoryBlock bytes;
            if (! file.loadFileAsData (bytes))
            {
                error = "Could not read " + file.getFullPathName();
                return false;
            }
            return read (bytes, patchJson, error);
        }
    }
}
