#pragma once

#include "bazalt/engine/AudioAnalysis.h"
#include "bazalt/engine/graph/SampleData.h"
#include <algorithm>
#include <limits>
#include <vector>

namespace bazalt::engine
{
    /** What the Sample factory finds in a sample (wiki/plans/Sampling.md 3a.2),
        with the same hearing the live analysis nodes use (AudioAnalysis.h).
        Offline: message / worker thread. */
    namespace sampleAnalysis
    {
        inline std::vector<float> mono (const SampleSource& source)
        {
            std::vector<float> out ((size_t) source.frames);
            for (int f = 0; f < source.frames; ++f)
            {
                float sum = 0.0f;
                for (int c = 0; c < source.channels; ++c)
                    sum += source.at (f, c);
                out[(size_t) f] = sum / (float) source.channels;
            }
            return out;
        }

        /** The sample's note (MIDI, to the cent): the median of the confident
            pitch estimates. NaN when it has no clear pitch (a drum, noise). */
        inline float rootPitch (const SampleSource& source)
        {
            if (source.frames < 256)
                return std::numeric_limits<float>::quiet_NaN();
            PitchTracker tracker;
            tracker.prepare (source.sampleRate);
            tracker.setRange (24.0f, 108.0f);
            std::vector<float> notes;
            for (const auto x : mono (source))
                if (tracker.push (x) && tracker.getConfidence() > 0.85f)
                    notes.push_back (tracker.getNote());
            if (notes.size() < 4)
                return std::numeric_limits<float>::quiet_NaN();
            std::nth_element (notes.begin(), notes.begin() + (long) notes.size() / 2, notes.end());
            return std::round (notes[notes.size() / 2] * 100.0f) / 100.0f;
        }

        /** Where the attacks are, in seconds — the slice points of "slice at
            transients". `sensitivityDb`: how far a rise must jump over the
            recent level (lower: more slices). */
        inline std::vector<double> transients (const SampleSource& source, float sensitivityDb = 6.0f, float minGapMs = 60.0f)
        {
            std::vector<double> times;
            OnsetDetector detector;
            detector.prepare (source.sampleRate);
            detector.setSensitivityDb (sensitivityDb);
            detector.setHoldMs (minGapMs);
            const auto samples = mono (source);
            const auto skip = (int) (0.01 * source.sampleRate); // the very start is the first slice anyway
            for (int f = 0; f < (int) samples.size(); ++f)
                if (detector.push (samples[(size_t) f]) && f > skip)
                    times.push_back ((double) f / source.sampleRate);
            return times;
        }
    }
}
