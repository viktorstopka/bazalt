#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>

/** The building blocks of space.diffuser and space.reverb (wiki/plans/Reverb.md),
    shared the way BandLimited.h is shared by the oscillators: a modulated
    fractional delay line, the two lossless mixing matrices, second-order
    shelves for per-band decay, a multichannel diffuser and a feedback delay
    network. Everything allocates in prepare() only; per-sample calls never do.
*/
namespace bazalt::engine::reverb
{
    inline constexpr int maxChannels = 16;
    inline constexpr double speedOfSound = 343.0; // m/s

    //==============================================================================
    /** A delay line read at a fractional, moving position: Catmull-Rom cubic
        for one-shot reads, lossless allpass interpolation for anything inside
        a loop (see readAllpass). Power-of-two buffer, masked index. */
    class DelayLine
    {
    public:
        void prepare (int maxDelaySamples)
        {
            int size = 4;
            while (size < maxDelaySamples + 4)
                size <<= 1;
            buffer.assign ((size_t) size, 0.0f);
            mask = size - 1;
            writeIndex = 0;
        }

        void reset() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); }

        void write (float value) noexcept
        {
            buffer[(size_t) writeIndex] = value;
            writeIndex = (writeIndex + 1) & mask;
        }

        /** `delay` samples behind the most recent write (>= 1). */
        float read (float delay) const noexcept
        {
            const auto clamped = juce::jlimit (1.0f, (float) (mask - 3), delay);
            const auto whole = (int) clamped;
            const auto t = clamped - (float) whole;
            const auto base = writeIndex - 1 - whole; // sample at `whole` samples back
            const auto ym1 = buffer[(size_t) ((base + 1) & mask)];
            const auto y0 = buffer[(size_t) (base & mask)];
            const auto y1 = buffer[(size_t) ((base - 1) & mask)];
            const auto y2 = buffer[(size_t) ((base - 2) & mask)];
            const auto c1 = 0.5f * (y1 - ym1);
            const auto c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
            const auto c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
            return ((c3 * t + c2) * t + c1) * t + y0;
        }

        /** A fractional read that loses nothing: first-order allpass
            interpolation (flat magnitude at every frequency). Inside a
            feedback loop any magnitude loss compounds every pass — cubic
            interpolation would quietly shorten the high-frequency decay and
            drain a frozen tail. The fractional part is kept in [0.5, 1.5) so
            the allpass coefficient stays in (-0.2, 0.33], far from its pole.
            `state` is this reader's own memory (its previous output). */
        float readAllpass (float delay, float& state) const noexcept
        {
            const auto clamped = juce::jlimit (1.5f, (float) (mask - 3), delay);
            const auto whole = (int) (clamped - 0.5f);
            const auto fraction = clamped - (float) whole;
            const auto eta = (1.0f - fraction) / (1.0f + fraction);
            const auto y = readInteger (whole + 1) + eta * (readInteger (whole) - state);
            state = y;
            return y;
        }

        /** Integer read, no interpolation (unmodulated taps). */
        float readInteger (int delay) const noexcept { return buffer[(size_t) ((writeIndex - 1 - delay) & mask)]; }

    private:
        std::vector<float> buffer;
        int mask = 0;
        int writeIndex = 0;
    };

    //==============================================================================
    /** In-place orthonormal Hadamard transform (N a power of two, <= 16). */
    inline void hadamard (float* x, int n) noexcept
    {
        for (int h = 1; h < n; h <<= 1)
            for (int i = 0; i < n; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const auto a = x[j];
                    const auto b = x[j + h];
                    x[j] = a + b;
                    x[j + h] = a - b;
                }
        const auto scale = 1.0f / std::sqrt ((float) n);
        for (int i = 0; i < n; ++i)
            x[i] *= scale;
    }

    /** In-place Householder reflection I - (2/N)·1·1ᵀ: lossless, O(N), and every
        line feeds every other — the classic FDN feedback matrix. */
    inline void householder (float* x, int n) noexcept
    {
        auto sum = 0.0f;
        for (int i = 0; i < n; ++i)
            sum += x[i];
        const auto correction = sum * (2.0f / (float) n);
        for (int i = 0; i < n; ++i)
            x[i] -= correction;
    }

    //==============================================================================
    /** RBJ second-order shelf, transposed direct form II. Gain is exact at DC
        and Nyquist (bilinear), half-way in dB at `frequency`. */
    class Shelf
    {
    public:
        enum class Kind { low, high };

        void set (Kind kind, double sampleRate, double frequency, double linearGain) noexcept
        {
            const auto a = std::sqrt (juce::jmax (1.0e-6, linearGain));
            const auto w0 = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, sampleRate * 0.45, frequency) / sampleRate;
            const auto cosW = std::cos (w0);
            const auto alpha = std::sin (w0) / 2.0 * std::sqrt (2.0); // S = 1 (Q = 1/sqrt 2)
            const auto twoSqrtAAlpha = 2.0 * std::sqrt (a) * alpha;

            double b0, b1, b2, a0, a1, a2;
            if (kind == Kind::low)
            {
                b0 = a * ((a + 1) - (a - 1) * cosW + twoSqrtAAlpha);
                b1 = 2 * a * ((a - 1) - (a + 1) * cosW);
                b2 = a * ((a + 1) - (a - 1) * cosW - twoSqrtAAlpha);
                a0 = (a + 1) + (a - 1) * cosW + twoSqrtAAlpha;
                a1 = -2 * ((a - 1) + (a + 1) * cosW);
                a2 = (a + 1) + (a - 1) * cosW - twoSqrtAAlpha;
            }
            else
            {
                b0 = a * ((a + 1) + (a - 1) * cosW + twoSqrtAAlpha);
                b1 = -2 * a * ((a - 1) + (a + 1) * cosW);
                b2 = a * ((a + 1) + (a - 1) * cosW - twoSqrtAAlpha);
                a0 = (a + 1) - (a - 1) * cosW + twoSqrtAAlpha;
                a1 = 2 * ((a - 1) - (a + 1) * cosW);
                a2 = (a + 1) - (a - 1) * cosW - twoSqrtAAlpha;
            }
            cb0 = (float) (b0 / a0);
            cb1 = (float) (b1 / a0);
            cb2 = (float) (b2 / a0);
            ca1 = (float) (a1 / a0);
            ca2 = (float) (a2 / a0);
        }

        float process (float x) noexcept
        {
            const auto y = cb0 * x + s1;
            s1 = cb1 * x - ca1 * y + s2;
            s2 = cb2 * x - ca2 * y;
            return y;
        }

        void reset() noexcept { s1 = s2 = 0.0f; }

    private:
        float cb0 = 1.0f, cb1 = 0.0f, cb2 = 0.0f, ca1 = 0.0f, ca2 = 0.0f;
        float s1 = 0.0f, s2 = 0.0f;
    };

    /** A one-pole low- or high-pass for input tone shaping. */
    class OnePole
    {
    public:
        void setLowpass (double sampleRate, double frequency) noexcept
        {
            coefficient = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * frequency / sampleRate));
            highpass = false;
        }
        void setHighpass (double sampleRate, double frequency) noexcept
        {
            setLowpass (sampleRate, frequency);
            highpass = true;
        }
        float process (float x) noexcept
        {
            state += coefficient * (x - state);
            return highpass ? x - state : state;
        }
        void reset() noexcept { state = 0.0f; }

    private:
        float coefficient = 1.0f;
        float state = 0.0f;
        bool highpass = false;
    };

    //==============================================================================
    /** Stereo <-> N channels, energy preserving: the input alternates L/R over
        the channels; the outputs read two orthogonal ±1 patterns (rows of a
        Hadamard matrix), so an evenly-spread field comes out decorrelated. */
    inline void upmix (float left, float right, float* channels, int n) noexcept
    {
        const auto scale = std::sqrt (2.0f / (float) n);
        for (int i = 0; i < n; ++i)
            channels[i] = ((i & 1) == 0 ? left : right) * scale;
    }

    inline void downmix (const float* channels, int n, float& left, float& right) noexcept
    {
        auto l = 0.0f, r = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            l += (i & 1) == 0 ? channels[i] : -channels[i];
            r += (i & 2) == 0 ? channels[i] : -channels[i];
        }
        const auto scale = 1.0f / std::sqrt (2.0f);
        left = l * scale;
        right = r * scale;
    }

    //==============================================================================
    /** Geraint Luff's multichannel diffuser: each stage delays every channel by
        a different (fixed, seeded) amount, flips some polarities and mixes all
        channels with a Hadamard matrix. A few stages turn a click into dense
        noise without colouring it — the whole thing is lossless. Stage spans
        grow geometrically and sum to `spanSamples`. */
    class Diffuser
    {
    public:
        static constexpr int maxStages = 6;

        /** Allocates every stage up to maxStages, so setStages() never has to. */
        void prepare (double sampleRate, int numChannels, double maxSpanSeconds, juce::int64 seed)
        {
            channels = juce::jlimit (2, maxChannels, numChannels);
            fs = sampleRate;
            spanInitialised = false;
            const auto maxSamples = (int) std::ceil (maxSpanSeconds * sampleRate) + 64;

            juce::Random random (seed);
            for (int s = 0; s < maxStages; ++s)
                for (int c = 0; c < channels; ++c)
                {
                    lines[(size_t) s][(size_t) c].prepare (maxSamples);
                    // Spread each channel over its own slice of the stage span,
                    // so no two channels share a delay.
                    position[(size_t) s][(size_t) c] = ((float) c + random.nextFloat()) / (float) channels;
                    flip[(size_t) s][(size_t) c] = random.nextBool();
                    lfoPhase[(size_t) s][(size_t) c] = random.nextFloat();
                    lfoRate[(size_t) s][(size_t) c] = 0.7f + 0.6f * random.nextFloat();
                }
        }

        void reset() noexcept
        {
            for (int s = 0; s < maxStages; ++s)
                for (int c = 0; c < channels; ++c)
                {
                    lines[(size_t) s][(size_t) c].reset();
                    readState[(size_t) s][(size_t) c] = 0.0f;
                }
            span = targetSpan;
        }

        void setStages (int numStages) noexcept { stages = juce::jlimit (1, maxStages, numStages); }

        /** Total delay span over all stages; the diffusion amount scales it. */
        void setSpan (float spanSamples) noexcept
        {
            targetSpan = juce::jmax (0.0f, spanSamples);
            if (! spanInitialised)
            {
                span = targetSpan; // the first value applies at once; later ones glide
                spanInitialised = true;
            }
        }

        /** Modulation depth in samples, and its rate. */
        void setModulation (float depthSamples, float rateHz) noexcept
        {
            modDepth = depthSamples;
            modIncrement = (float) (rateHz / fs);
        }

        int getNumChannels() const noexcept { return channels; }

        void process (float* x) noexcept
        {
            // Glide toward a new span, like a tape speed change, never a jump.
            span += (targetSpan - span) * 0.0005f;
            const auto denominator = (float) ((1 << stages) - 1);

            for (int s = 0; s < stages; ++s)
            {
                const auto stageSpan = span * (float) (1 << s) / denominator;
                for (int c = 0; c < channels; ++c)
                {
                    auto& line = lines[(size_t) s][(size_t) c];
                    line.write (x[c]);

                    auto& phase = lfoPhase[(size_t) s][(size_t) c];
                    phase += modIncrement * lfoRate[(size_t) s][(size_t) c];
                    phase -= std::floor (phase);
                    const auto wobble = modDepth * std::sin (juce::MathConstants<float>::twoPi * phase);

                    const auto delay = 1.5f + stageSpan * position[(size_t) s][(size_t) c] + modDepth + wobble;
                    const auto y = line.readAllpass (delay, readState[(size_t) s][(size_t) c]);
                    x[c] = flip[(size_t) s][(size_t) c] ? -y : y;
                }
                hadamard (x, channels);
            }
        }

    private:
        int channels = 8, stages = 4;
        double fs = 48000.0;
        float span = 0.0f, targetSpan = 0.0f;
        bool spanInitialised = false;
        float modDepth = 0.0f, modIncrement = 0.0f;
        std::array<std::array<DelayLine, maxChannels>, maxStages> lines;
        std::array<std::array<float, maxChannels>, maxStages> position {};
        std::array<std::array<bool, maxChannels>, maxStages> flip {};
        std::array<std::array<float, maxChannels>, maxStages> readState {};
        std::array<std::array<float, maxChannels>, maxStages> lfoPhase {};
        std::array<std::array<float, maxChannels>, maxStages> lfoRate {};
    };

    //==============================================================================
    /** Jot's feedback delay network: N delay lines, a lossless Householder
        feedback matrix, and per-line low/high shelves whose gains are derived
        from the target decay time per band, so the measured RT60 is what the
        controls say: g = 10^(-3·L / (RT60·fs)) for a line of L samples. Each
        line's read position wanders slowly (modulation) so the fixed modes
        that make cheap reverbs ring "metallic" are smeared. */
    class FeedbackDelayNetwork
    {
    public:
        static constexpr double lowCrossoverHz = 250.0;
        static constexpr double highCrossoverHz = 3000.0;

        void prepare (double sampleRate, int numLines, double maxLineSeconds, juce::int64 seed)
        {
            fs = sampleRate;
            lines = juce::jlimit (2, maxChannels, numLines);
            const auto maxSamples = (int) std::ceil (maxLineSeconds * sampleRate) + 256;

            juce::Random random (seed);
            for (int i = 0; i < lines; ++i)
            {
                delays[(size_t) i].prepare (maxSamples);
                lfoPhase[(size_t) i] = random.nextFloat();
                lfoRate[(size_t) i] = 0.6f + 0.8f * random.nextFloat();
                // Lengths spread geometrically over [1, 2] of the room's base
                // delay, nudged off any common pattern by a little jitter.
                const auto t = ((float) i + 0.5f * random.nextFloat()) / (float) lines;
                lengthRatio[(size_t) i] = std::pow (2.0f, t);
            }
            reset();
        }

        void reset() noexcept
        {
            for (int i = 0; i < lines; ++i)
            {
                delays[(size_t) i].reset();
                readState[(size_t) i] = 0.0f;
                lowShelf[(size_t) i].reset();
                highShelf[(size_t) i].reset();
            }
            lengthsInitialised = false;
        }

        int getNumLines() const noexcept { return lines; }

        /** The longest line, in samples; the others follow `lengthRatio`. */
        void setBaseLength (float samples) noexcept
        {
            baseLength = juce::jmax (8.0f, samples);
            if (! lengthsInitialised)
            {
                for (int i = 0; i < lines; ++i)
                    currentLength[(size_t) i] = targetLengthOf (i);
                lengthsInitialised = true;
            }
        }

        void setModulation (float depthSamples, float rateHz) noexcept
        {
            modDepth = depthSamples;
            modIncrement = (float) (rateHz / fs);
        }

        /** Per-band decay times in seconds; <= 0 or `frozen` means no loss. */
        void setDecay (float lowSeconds, float midSeconds, float highSeconds, bool frozen) noexcept
        {
            for (int i = 0; i < lines; ++i)
            {
                const auto length = (double) targetLengthOf (i);
                const auto gainFor = [&] (float seconds)
                {
                    return frozen ? 1.0 : std::pow (10.0, -3.0 * length / (juce::jmax (0.01, (double) seconds) * fs));
                };
                const auto mid = gainFor (midSeconds);
                midGain[(size_t) i] = (float) mid;
                lowShelf[(size_t) i].set (Shelf::Kind::low, fs, lowCrossoverHz, gainFor (lowSeconds) / mid);
                highShelf[(size_t) i].set (Shelf::Kind::high, fs, highCrossoverHz, gainFor (highSeconds) / mid);
            }
        }

        /** Mean per-pass gain of the mid band — for input normalisation. */
        float getMeanMidGain() const noexcept
        {
            auto sum = 0.0f;
            for (int i = 0; i < lines; ++i)
                sum += midGain[(size_t) i];
            return sum / (float) lines;
        }

        /** `input`/`output` are `getNumLines()` channels each. */
        void process (const float* input, float* output) noexcept
        {
            std::array<float, maxChannels> feedback {};

            for (int i = 0; i < lines; ++i)
            {
                // A size change glides the read position (a deliberate tape-like
                // pitch smear) rather than jumping.
                auto& length = currentLength[(size_t) i];
                length += (targetLengthOf (i) - length) * 0.0002f;

                auto& phase = lfoPhase[(size_t) i];
                phase += modIncrement * lfoRate[(size_t) i];
                phase -= std::floor (phase);
                const auto wobble = modDepth * std::sin (juce::MathConstants<float>::twoPi * phase);

                const auto y = delays[(size_t) i].readAllpass (length + wobble, readState[(size_t) i]);
                output[i] = y;
                feedback[(size_t) i] = highShelf[(size_t) i].process (lowShelf[(size_t) i].process (y)) * midGain[(size_t) i];
            }

            householder (feedback.data(), lines);

            for (int i = 0; i < lines; ++i)
            {
                auto value = feedback[(size_t) i] + input[i];
                if (std::abs (value) < 1.0e-20f) // a decaying network walks into denormals by design
                    value = 0.0f;
                delays[(size_t) i].write (value);
            }
        }

    private:
        float targetLengthOf (int i) const noexcept { return baseLength * lengthRatio[(size_t) i]; }

        double fs = 48000.0;
        int lines = 8;
        float baseLength = 1000.0f;
        bool lengthsInitialised = false;
        float modDepth = 0.0f, modIncrement = 0.0f;
        std::array<DelayLine, maxChannels> delays;
        std::array<Shelf, maxChannels> lowShelf, highShelf;
        std::array<float, maxChannels> midGain {}, lengthRatio {}, currentLength {}, lfoPhase {}, lfoRate {}, readState {};
    };
}
