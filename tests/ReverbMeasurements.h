#pragma once

// Measurement tools for space.reverb / space.diffuser (wiki/plans/Reverb.md §5):
// render an impulse response, then measure what a listener would — decay time
// per band (Schroeder backward integration), how fast echoes become dense
// (Abel & Huang's normalised echo density), whether the tail rings at fixed
// frequencies, and how the two channels relate.
#include "bazalt/engine/graph/Node.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace reverbtest
{
    using bazalt::engine::Node;

    struct Stereo
    {
        std::vector<float> left, right;
    };

    /** Runs `node` (stereo in at flat inputs 0/1, every other input
        unconnected) over `input`, in blocks of `blockSize`. */
    inline Stereo render (Node& node, const Stereo& input, int blockSize = 256)
    {
        const auto numSamples = (int) input.left.size();
        const auto numInputs = node.getNumInputChannels();
        Stereo out { std::vector<float> ((size_t) numSamples), std::vector<float> ((size_t) numSamples) };
        std::vector<float> nan ((size_t) blockSize, std::numeric_limits<float>::quiet_NaN());

        for (int start = 0; start < numSamples; start += blockSize)
        {
            const auto n = std::min (blockSize, numSamples - start);
            std::vector<const float*> ins ((size_t) numInputs, nan.data());
            ins[0] = input.left.data() + start;
            ins[1] = input.right.data() + start;
            float* outs[2] = { out.left.data() + start, out.right.data() + start };
            node.processBlock (ins.data(), outs, n);
        }
        return out;
    }

    inline Stereo impulse (double seconds, double sampleRate, bool left = true, bool right = true)
    {
        Stereo in { std::vector<float> ((size_t) (seconds * sampleRate)), std::vector<float> ((size_t) (seconds * sampleRate)) };
        in.left[0] = left ? 1.0f : 0.0f;
        in.right[0] = right ? 1.0f : 0.0f;
        return in;
    }

    /** An RBJ band-pass (0 dB peak), run forward then backward (zero phase). */
    inline std::vector<float> band (const std::vector<float>& x, double sampleRate, double centreHz, double q = 1.414)
    {
        const auto w0 = juce::MathConstants<double>::twoPi * centreHz / sampleRate;
        const auto alpha = std::sin (w0) / (2.0 * q);
        const auto a0 = 1.0 + alpha;
        const auto b0 = alpha / a0, b2 = -alpha / a0, a1 = -2.0 * std::cos (w0) / a0, a2 = (1.0 - alpha) / a0;

        const auto pass = [&] (std::vector<double>& v)
        {
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (auto& s : v)
            {
                const auto y = b0 * s + b2 * x2 - a1 * y1 - a2 * y2;
                x2 = x1;
                x1 = s;
                y2 = y1;
                y1 = y;
                s = y;
            }
        };
        std::vector<double> v (x.begin(), x.end());
        pass (v);
        std::reverse (v.begin(), v.end());
        pass (v);
        std::reverse (v.begin(), v.end());
        return { v.begin(), v.end() };
    }

    /** RT60 from the Schroeder energy decay curve, fitted between -5 and
        -35 dB (T30), in seconds. Negative if the curve never gets that low. */
    inline double rt60 (const std::vector<float>& h, double sampleRate, double fromDb = -5.0, double toDb = -35.0)
    {
        std::vector<double> edc (h.size());
        double sum = 0;
        for (size_t i = h.size(); i-- > 0;)
        {
            sum += (double) h[i] * h[i];
            edc[i] = sum;
        }
        if (sum <= 0)
            return -1;

        // Least-squares line through the EDC (in dB) between the two levels.
        double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (size_t i = 0; i < edc.size(); ++i)
        {
            const auto db = 10.0 * std::log10 (edc[i] / edc[0] + 1e-30);
            if (db > fromDb)
                continue;
            if (db < toDb)
                break;
            const auto t = (double) i / sampleRate;
            n += 1;
            sx += t;
            sy += db;
            sxx += t * t;
            sxy += t * db;
        }
        if (n < 10)
            return -1;
        const auto slope = (n * sxy - sx * sy) / (n * sxx - sx * sx); // dB per second
        return slope < 0 ? -60.0 / slope : -1;
    }

    /** First time (s) the normalised echo density (20 ms window) reaches `level`. */
    inline double echoDensityTime (const std::vector<float>& h, double sampleRate, double level = 0.9)
    {
        const auto window = (int) (0.02 * sampleRate);
        const auto expected = std::erfc (1.0 / std::sqrt (2.0)); // fraction above 1 sigma for Gaussian noise
        for (int start = 0; start + window < (int) h.size(); start += window / 4)
        {
            double energy = 0;
            for (int i = start; i < start + window; ++i)
                energy += (double) h[(size_t) i] * h[(size_t) i];
            const auto sigma = std::sqrt (energy / window);
            if (sigma <= 0)
                continue;
            int above = 0;
            for (int i = start; i < start + window; ++i)
                above += std::abs (h[(size_t) i]) > sigma ? 1 : 0;
            if ((double) above / window / expected >= level)
                return (start + window / 2) / sampleRate;
        }
        return -1;
    }

    /** Ringing: the tallest spectral peak of a tail segment relative to the
        median of its own neighbourhood (±1/6 octave), 100 Hz–10 kHz, in dB.
        Noise-like tails sit around 12–15 dB; a metallic tail shows narrow
        peaks far above that. */
    inline double peakToLocalMedianDb (const std::vector<float>& h, double sampleRate, double fromSeconds, double seconds, double* peakHz = nullptr)
    {
        constexpr int order = 15;
        constexpr int size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> buffer ((size_t) size * 2, 0.0f);
        const auto start = (size_t) (fromSeconds * sampleRate);
        const auto length = std::min ((size_t) (seconds * sampleRate), std::min ((size_t) size, h.size() - start));
        for (size_t i = 0; i < length; ++i)
        {
            const auto w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (length - 1));
            buffer[i] = h[start + i] * w;
        }
        fft.performFrequencyOnlyForwardTransform (buffer.data());

        const auto binHz = sampleRate / size;
        double worst = 0;
        std::vector<float> neighbourhood;
        for (int bin = (int) (100.0 / binHz); bin < (int) (10000.0 / binHz); bin += 4)
        {
            const auto lo = (int) (bin / std::pow (2.0, 1.0 / 6.0));
            const auto hi = (int) (bin * std::pow (2.0, 1.0 / 6.0));
            neighbourhood.assign (buffer.begin() + lo, buffer.begin() + hi + 1);
            for (auto& m : neighbourhood)
                m *= m;
            std::nth_element (neighbourhood.begin(), neighbourhood.begin() + (long) neighbourhood.size() / 2, neighbourhood.end());
            const auto median = (double) neighbourhood[neighbourhood.size() / 2];
            const auto power = (double) buffer[(size_t) bin] * buffer[(size_t) bin];
            if (median > 0 && 10.0 * std::log10 (power / median) > worst)
            {
                worst = 10.0 * std::log10 (power / median);
                if (peakHz != nullptr)
                    *peakHz = bin * binHz;
            }
        }
        return worst;
    }

    inline double correlation (const Stereo& s, double sampleRate, double fromSeconds, double toSeconds)
    {
        double lr = 0, ll = 0, rr = 0;
        for (auto i = (size_t) (fromSeconds * sampleRate); i < std::min (s.left.size(), (size_t) (toSeconds * sampleRate)); ++i)
        {
            lr += (double) s.left[i] * s.right[i];
            ll += (double) s.left[i] * s.left[i];
            rr += (double) s.right[i] * s.right[i];
        }
        return lr / std::sqrt (ll * rr + 1e-30);
    }

    /** Energy of the mono sum (L+R)/2 relative to the mean channel energy, dB. */
    inline double monoSumDb (const Stereo& s)
    {
        double mono = 0, channels = 0;
        for (size_t i = 0; i < s.left.size(); ++i)
        {
            const auto m = 0.5 * ((double) s.left[i] + s.right[i]);
            mono += m * m;
            channels += 0.5 * ((double) s.left[i] * s.left[i] + (double) s.right[i] * s.right[i]);
        }
        return 10.0 * std::log10 (mono / (channels + 1e-30));
    }

    inline double rmsDb (const std::vector<float>& x, double sampleRate, double fromSeconds, double toSeconds)
    {
        double sum = 0;
        const auto a = (size_t) (fromSeconds * sampleRate), b = std::min (x.size(), (size_t) (toSeconds * sampleRate));
        for (auto i = a; i < b; ++i)
            sum += (double) x[i] * x[i];
        return 10.0 * std::log10 (sum / (double) std::max<size_t> (1, b - a) + 1e-30);
    }
}
