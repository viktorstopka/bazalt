#pragma once

#include "bazalt/engine/graph/SampleData.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

namespace bazalt
{
    /** The sample library (wiki/plans/Sampling.md §2.2): every sample a patch
        uses, stored once by id (a hash of the file it came from) as FLAC in
        the per-user app-data folder, shared by the VST3 and the Standalone:
          Windows  %APPDATA%\Bazalt\Samples
          macOS    ~/Library/Bazalt/Samples
          Linux    ~/.config/Bazalt/Samples
        A patch only names samples by id; loading one decodes it from here into
        the process-wide SampleStore the engine's Sample nodes read.

        Decoding lives here, not in the engine: audio file formats are a host
        concern (CLAUDE.md rule 4). Message thread (the WebView handlers and
        GraphEditController) — decoding a long file takes a moment, never the
        audio thread. */
    class SampleLibrary
    {
    public:
        static constexpr double maxSeconds = 20.0 * 60.0; // a twenty-minute recording, at most
        static constexpr int maxChannels = 8; // FLAC's limit; many-channel samples (bakes) are a later step

        struct ImportResult
        {
            bool success = false;
            juce::String errorMessage;
            std::shared_ptr<const bazalt::engine::SampleSource> source;
        };

        explicit SampleLibrary (juce::File directoryToUse = defaultDirectory()) : directory (std::move (directoryToUse)) {}

        static juce::File defaultDirectory()
        {
            return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Bazalt").getChildFile ("Samples");
        }

        const juce::File& getDirectory() const noexcept { return directory; }
        juce::File fileFor (const juce::String& id) const { return directory.getChildFile (id + ".flac"); }

        /** A file the user dropped: decoded, stored in the library under the
            hash of its bytes, and loaded. Importing the same file twice gives
            the same id and stores it once. */
        ImportResult importFile (const juce::String& fileName, const juce::MemoryBlock& bytes) const
        {
            return importAs (bazalt::engine::sampleIdForBytes (bytes.getData(), bytes.getSize()), fileName, bytes);
        }

        /** Bytes for a known id — a sample embedded in a patch ("save with
            samples"), stored under the id the patch refers to. */
        ImportResult importAs (const juce::String& id, const juce::String& name, const juce::MemoryBlock& bytes) const
        {
            ImportResult result;
            auto source = decode (bytes, id, name, result.errorMessage);
            if (source == nullptr)
                return result;
            if (! fileFor (id).existsAsFile() && ! store (*source, result.errorMessage))
                return result;
            bazalt::engine::SampleStore::instance().add (source);
            result.source = std::move (source);
            result.success = true;
            return result;
        }

        /** A recording (Freeze, Capture this): stored under the hash of its
            samples and loaded, like a dropped file. Interleaved frames. */
        ImportResult addRecording (const juce::String& name, int channels, double sampleRate, std::vector<float> interleaved) const
        {
            ImportResult result;
            if (channels < 1 || channels > maxChannels || interleaved.size() < (size_t) channels || sampleRate <= 0.0)
            {
                result.errorMessage = "Nothing recorded yet";
                return result;
            }
            auto source = std::make_shared<bazalt::engine::SampleSource>();
            source->id = bazalt::engine::sampleIdForBytes (interleaved.data(), interleaved.size() * sizeof (float));
            source->name = name;
            source->channels = channels;
            source->frames = (int) (interleaved.size() / (size_t) channels);
            source->sampleRate = sampleRate;
            source->data = std::move (interleaved);
            if (! fileFor (source->id).existsAsFile() && ! store (*source, result.errorMessage))
                return result;
            bazalt::engine::SampleStore::instance().add (source);
            result.source = std::move (source);
            result.success = true;
            return result;
        }

        /** True once `id` is in the SampleStore, loading it from the library
            if it isn't yet. False: the library doesn't have it (missing). */
        bool ensureLoaded (const juce::String& id, const juce::String& name = {}) const
        {
            if (id.isEmpty())
                return false;
            if (bazalt::engine::SampleStore::instance().contains (id))
                return true;
            juce::MemoryBlock bytes;
            if (! fileFor (id).loadFileAsData (bytes))
                return false;
            juce::String error;
            auto source = decode (bytes, id, name, error);
            if (source == nullptr)
                return false;
            bazalt::engine::SampleStore::instance().add (std::move (source));
            return true;
        }

        /** The stored FLAC for `id` (for embedding in an exported patch);
            empty when the library doesn't have it. */
        juce::MemoryBlock storedBytes (const juce::String& id) const
        {
            juce::MemoryBlock bytes;
            fileFor (id).loadFileAsData (bytes);
            return bytes;
        }

    private:
        static std::shared_ptr<bazalt::engine::SampleSource> decode (const juce::MemoryBlock& bytes, const juce::String& id,
                                                                     const juce::String& name, juce::String& error)
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (
                formats.createReaderFor (std::make_unique<juce::MemoryInputStream> (bytes, false)));
            if (reader == nullptr)
            {
                error = "Not an audio file Bazalt can read (WAV, AIFF, FLAC, OGG or MP3)";
                return nullptr;
            }
            const auto channels = (int) reader->numChannels;
            const auto frames = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (maxSeconds * reader->sampleRate));
            if (channels < 1 || channels > maxChannels || frames < 1 || reader->sampleRate <= 0.0)
            {
                error = "That file has no audio Bazalt can use";
                return nullptr;
            }

            juce::AudioBuffer<float> buffer (channels, frames);
            if (! reader->read (&buffer, 0, frames, 0, true, true))
            {
                error = "Couldn't read the audio in that file";
                return nullptr;
            }

            auto source = std::make_shared<bazalt::engine::SampleSource>();
            source->id = id;
            source->name = name;
            source->channels = channels;
            source->frames = frames;
            source->sampleRate = reader->sampleRate;
            source->data.resize ((size_t) frames * (size_t) channels);
            for (int c = 0; c < channels; ++c)
            {
                const auto* in = buffer.getReadPointer (c);
                for (int f = 0; f < frames; ++f)
                    source->data[(size_t) f * (size_t) channels + (size_t) c] = in[f];
            }
            return source;
        }

        bool store (const bazalt::engine::SampleSource& source, juce::String& error) const
        {
            if (! directory.exists() && ! directory.createDirectory())
            {
                error = "Could not create " + directory.getFullPathName();
                return false;
            }
            const auto file = fileFor (source.id);
            const auto temp = file.getSiblingFile (file.getFileName() + ".part");
            {
                auto stream = std::make_unique<juce::FileOutputStream> (temp);
                if (! stream->openedOk())
                {
                    error = "Could not write " + temp.getFullPathName();
                    return false;
                }
                juce::FlacAudioFormat flac;
                std::unique_ptr<juce::AudioFormatWriter> writer (
                    flac.createWriterFor (stream.get(), source.sampleRate, (unsigned int) source.channels, 24, {}, 0));
                if (writer == nullptr)
                {
                    error = "Could not store that sample as FLAC";
                    return false;
                }
                stream.release(); // the writer owns it now
                juce::AudioBuffer<float> buffer (source.channels, source.frames);
                for (int c = 0; c < source.channels; ++c)
                    for (int f = 0; f < source.frames; ++f)
                        buffer.setSample (c, f, source.at (f, c));
                if (! writer->writeFromAudioSampleBuffer (buffer, 0, source.frames))
                {
                    error = "Could not write " + temp.getFullPathName();
                    return false;
                }
            }
            if (! temp.moveFileTo (file))
            {
                error = "Could not store " + file.getFullPathName();
                return false;
            }
            return true;
        }

        juce::File directory;
    };
}
