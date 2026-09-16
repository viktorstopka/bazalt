#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "bazalt/engine/PolyBlepOscillator.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>

namespace
{
    // Naive (non-bandlimited) sawtooth for comparison — samples the ideal
    // continuous ramp directly, with no anti-aliasing correction.
    float renderNaiveSaw (double& phase, double phaseIncrement) noexcept
    {
        const auto value = (float) (2.0 * phase - 1.0);
        phase += phaseIncrement;
        if (phase >= 1.0)
            phase -= 1.0;
        return value;
    }

    // Sums squared FFT magnitude for bins at or above lowFreqHz.
    double highBandEnergy (const std::vector<float>& magnitudes, double sampleRate, int fftSize, double lowFreqHz)
    {
        double energy = 0.0;
        const auto binHz = sampleRate / (double) fftSize;
        const auto firstBin = (int) std::ceil (lowFreqHz / binHz);

        for (int bin = firstBin; bin < fftSize / 2; ++bin)
            energy += (double) magnitudes[(size_t) bin] * (double) magnitudes[(size_t) bin];

        return energy;
    }
}

TEST_CASE ("PolyBLEP saw has far less near-Nyquist energy than a naive saw", "[engine][PolyBlepOscillator][aliasing]")
{
    constexpr double sampleRate = 44100.0;
    constexpr float frequencyHz = 5000.0f;
    constexpr int fftOrder = 12;
    constexpr int fftSize = 1 << fftOrder; // 4096

    bazalt::engine::PolyBlepOscillator oscillator;
    oscillator.prepare (sampleRate);
    oscillator.setWaveform (bazalt::engine::OscillatorWaveform::Saw);
    oscillator.setFrequency (frequencyHz);

    juce::dsp::FFT fft (fftOrder);
    juce::dsp::WindowingFunction<float> window (fftSize, juce::dsp::WindowingFunction<float>::hann);

    std::vector<float> polyBlepData ((size_t) fftSize * 2, 0.0f);
    std::vector<float> naiveData ((size_t) fftSize * 2, 0.0f);

    double naivePhase = 0.0;
    const double phaseIncrement = (double) frequencyHz / sampleRate;

    for (int i = 0; i < fftSize; ++i)
    {
        polyBlepData[(size_t) i] = oscillator.renderNextSample();
        naiveData[(size_t) i] = renderNaiveSaw (naivePhase, phaseIncrement);
    }

    window.multiplyWithWindowingTable (polyBlepData.data(), (size_t) fftSize);
    window.multiplyWithWindowingTable (naiveData.data(), (size_t) fftSize);

    fft.performFrequencyOnlyForwardTransform (polyBlepData.data(), true);
    fft.performFrequencyOnlyForwardTransform (naiveData.data(), true);

    // Top quartile of the spectrum, near Nyquist — where a naive saw's
    // aliased harmonics pile up and a bandlimited one should not.
    const auto lowFreqHz = sampleRate * 0.5 * 0.75;

    const auto polyBlepEnergy = highBandEnergy (polyBlepData, sampleRate, fftSize, lowFreqHz);
    const auto naiveEnergy = highBandEnergy (naiveData, sampleRate, fftSize, lowFreqHz);

    INFO ("PolyBLEP near-Nyquist energy: " << polyBlepEnergy);
    INFO ("Naive near-Nyquist energy: " << naiveEnergy);

    // The near-Nyquist band also carries the saw's own legitimate high
    // harmonics (a saw's spectrum falls off as 1/n regardless of aliasing),
    // which dilutes how much of this band's energy is actually aliasing —
    // so the bound is deliberately modest (measured ~7x in practice).
    REQUIRE (naiveEnergy > 0.0);
    CHECK (polyBlepEnergy < naiveEnergy * 0.35); // at least ~3x (-4.7 dB) less
}

TEST_CASE ("PolyBlepOscillator never produces non-finite samples across waveforms", "[engine][PolyBlepOscillator]")
{
    const auto waveform = GENERATE (bazalt::engine::OscillatorWaveform::Sine,
                                     bazalt::engine::OscillatorWaveform::Saw,
                                     bazalt::engine::OscillatorWaveform::Square,
                                     bazalt::engine::OscillatorWaveform::Triangle);

    bazalt::engine::PolyBlepOscillator oscillator;
    oscillator.prepare (44100.0);
    oscillator.setWaveform (waveform);
    oscillator.setFrequency (220.0f);

    for (int i = 0; i < 44100; ++i)
    {
        const auto sample = oscillator.renderNextSample();
        REQUIRE (std::isfinite (sample));
        REQUIRE (std::abs (sample) < 10.0f);
    }
}
