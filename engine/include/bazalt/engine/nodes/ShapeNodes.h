#pragma once

#include "bazalt/engine/graph/Node.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** First-order antiderivative antialiasing (ADAA, Parker/Esqueda/Bilbao
        2016): instead of f(x[n]), output the average of f over the straight
        line from x[n-1] to x[n] — (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]),
        with F the antiderivative of f. Most of the aliasing a hard curve
        would fold back disappears, at the cost of half a sample of delay and
        a gentle top-octave roll-off — with no oversampling, no latency, no
        block dependence, and one sample at a time (so a shaper also works in
        a feedback loop and per channel lane). Computed in double: F's of
        large arguments differ by small amounts. */
    namespace adaa
    {
        template <typename Fn, typename AntiFn>
        double firstOrder (double x, double previousX, Fn f, AntiFn antiderivative) noexcept
        {
            const auto dx = x - previousX;
            if (std::abs (dx) < 1.0e-6)
                return f (0.5 * (x + previousX));
            return (antiderivative (x) - antiderivative (previousX)) / dx;
        }

        /** log(cosh(x)), the antiderivative of tanh, without overflowing. */
        inline double logCosh (double x) noexcept
        {
            const auto a = std::abs (x);
            return a + std::log1p (std::exp (-2.0 * a)) - 0.6931471805599453;
        }
    }

    //==============================================================================
    /** Stable type id: "shape.rectify". Half-wave (negative half removed) or
        full-wave (absolute value) rectification, per channel, antialiased —
        octave-up and "buzz" tones from anything. Rectifying adds DC by nature;
        follow with util.dcBlock when that matters. */
    class ShapeRectifyNode : public Node
    {
    public:
        enum class Mode { Half, Full };

        void reset() override { previousX = 0.0; }

        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 1; }
        juce::String getTitle() const override { return "Rectify"; }
        juce::String getCategory() const override { return "Shape"; }

        std::vector<PortDescriptor> getInputPorts() const override { return { perChannel ({ .id = "in", .type = SignalType::Signal, .quantity = Quantity::Audio }) }; }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { perChannel (PortDescriptor { .id = "out", .type = SignalType::Signal, .label = "Out", .isPrimaryOutput = true, .quantity = Quantity::Audio }) };
        }
        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "shape.rectify.mode", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f,
                                            .displayName = "Mode", .isInteger = true, .kind = ValueKind::Enum,
                                            .enumOptions = { { "half", "Half" }, { "full", "Full" } } } };
        }
        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "shape.rectify.mode")
                mode = value > 0.5f ? Mode::Full : Mode::Half;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto x = (double) inputs[0];
            if (mode == Mode::Full)
                outputs[0] = (float) adaa::firstOrder (x, previousX, [] (double v) { return std::abs (v); },
                                                       [] (double v) { return 0.5 * v * std::abs (v); });
            else
                outputs[0] = (float) adaa::firstOrder (x, previousX, [] (double v) { return v > 0.0 ? v : 0.0; },
                                                       [] (double v) { return v > 0.0 ? 0.5 * v * v : 0.0; });
            previousX = x;
        }

    private:
        Mode mode = Mode::Full;
        double previousX = 0.0;
    };

    //==============================================================================
    /** Stable type id: "shape.crush". Bit depth and sample-rate reduction —
        deliberately not antialiased, the grit is the point. `bits` is
        continuous (fractional bits give in-between step sizes, so it sweeps
        smoothly); `rate` is the held sample rate in Hz, counted by a phase
        accumulator from prepare() — independent of host block size. */
    class ShapeCrushNode : public Node
    {
    public:
        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            reset();
        }
        void reset() override
        {
            holdPhase = 1.0;
            held = 0.0f;
        }

        int getNumInputPorts() const noexcept override { return 3; }
        int getNumOutputPorts() const noexcept override { return 1; }
        juce::String getTitle() const override { return "Crush"; }
        juce::String getCategory() const override { return "Shape"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                perChannel ({ .id = "in", .type = SignalType::Signal, .quantity = Quantity::Audio }),
                PortDescriptor { .id = "shape.crush.bits", .type = SignalType::Signal, .label = "Bits",
                                  .minValue = 1.0f, .maxValue = 24.0f, .defaultValue = 8.0f, .hasFallbackWhenUnconnected = true },
                PortDescriptor { .id = "shape.crush.rate", .type = SignalType::Signal, .label = "Rate", .unit = "Hz",
                                  .minValue = 50.0f, .maxValue = 48000.0f, .defaultValue = 48000.0f, .isLogScale = true,
                                  .hasFallbackWhenUnconnected = true, .quantity = Quantity::Frequency, .curve = Curve::Logarithmic },
            };
        }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { perChannel (PortDescriptor { .id = "out", .type = SignalType::Signal, .label = "Out", .isPrimaryOutput = true, .quantity = Quantity::Audio }) };
        }
        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "shape.crush.bits")
                storedBits = juce::jlimit (1.0f, 24.0f, value);
            else if (parameterId == "shape.crush.rate")
                storedRate = juce::jlimit (50.0f, 192000.0f, value);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto bits = std::isnan (inputs[1]) ? storedBits : juce::jlimit (1.0f, 24.0f, inputs[1]);
            const auto rate = std::isnan (inputs[2]) ? storedRate : juce::jlimit (50.0f, 192000.0f, inputs[2]);

            holdPhase += sampleRate > 0.0 ? (double) rate / sampleRate : 1.0;
            if (holdPhase >= 1.0)
            {
                holdPhase -= std::floor (holdPhase);
                held = inputs[0];
            }

            const auto steps = std::exp2 (bits - 1.0f);
            outputs[0] = std::round (held * steps) / steps;
        }

    private:
        double sampleRate = 48000.0, holdPhase = 1.0;
        float held = 0.0f, storedBits = 8.0f, storedRate = 48000.0f;
    };

    //==============================================================================
    /** Stable type id: "shape.waveshaper". Drive into one of six transfer
        curves, antialiased (ADAA): tanh (smooth, symmetric), cubic (gentlest
        knee, flat beyond ±1), hard (a clean clip), algebraic (x/√(1+x²),
        softer than tanh), asymmetric (tanh on top, a softer half-amplitude
        curve below — even harmonics, "tube-ish"), sine (folds back past ±π/2).
        `bias` shifts the operating point (asymmetry, even harmonics); the
        static DC it causes is removed. `mix` blends with the dry signal.
        The custom-curve input arrives with factory.curve (Factories.md). */
    class ShapeWaveshaperNode : public Node
    {
    public:
        enum class Curve { Tanh, Cubic, Hard, Algebraic, Asymmetric, Sine };

        void reset() override { previousX = 0.0; }

        int getNumInputPorts() const noexcept override { return 5; }
        int getNumOutputPorts() const noexcept override { return 1; }
        juce::String getTitle() const override { return "Waveshaper"; }
        juce::String getCategory() const override { return "Shape"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                perChannel ({ .id = "in", .type = SignalType::Signal, .quantity = Quantity::Audio }),
                PortDescriptor { .id = "shape.waveshaper.drive", .type = SignalType::Signal, .label = "Drive", .unit = "dB",
                                  .minValue = 0.0f, .maxValue = 36.0f, .defaultValue = 6.0f, .hasFallbackWhenUnconnected = true,
                                  .quantity = Quantity::Gain },
                PortDescriptor { .id = "shape.waveshaper.bias", .type = SignalType::Signal, .label = "Bias",
                                  .minValue = -1.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .hasFallbackWhenUnconnected = true,
                                  .quantity = Quantity::Bipolar, .polarity = Polarity::Bipolar },
                PortDescriptor { .id = "shape.waveshaper.output", .type = SignalType::Signal, .label = "Output", .unit = "dB",
                                  .minValue = -24.0f, .maxValue = 12.0f, .defaultValue = 0.0f, .hasFallbackWhenUnconnected = true,
                                  .quantity = Quantity::Gain },
                PortDescriptor { .id = "shape.waveshaper.mix", .type = SignalType::Signal, .label = "Mix",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .hasFallbackWhenUnconnected = true,
                                  .quantity = Quantity::Unipolar },
            };
        }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { perChannel (PortDescriptor { .id = "out", .type = SignalType::Signal, .label = "Out", .isPrimaryOutput = true, .quantity = Quantity::Audio }) };
        }
        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "shape.waveshaper.curve", .minValue = 0.0f, .maxValue = 5.0f, .defaultValue = 0.0f,
                                            .displayName = "Curve", .isInteger = true, .kind = ValueKind::Enum,
                                            .enumOptions = { { "tanh", "Tanh" }, { "cubic", "Cubic" }, { "hard", "Hard" },
                                                              { "algebraic", "Algebraic" }, { "asymmetric", "Asymmetric" },
                                                              { "sine", "Sine" } } } };
        }
        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "shape.waveshaper.curve")
                curve = (Curve) juce::jlimit (0, 5, (int) std::lround (value));
            else if (parameterId == "shape.waveshaper.drive")
                storedDrive = value;
            else if (parameterId == "shape.waveshaper.bias")
                storedBias = value;
            else if (parameterId == "shape.waveshaper.output")
                storedOutput = value;
            else if (parameterId == "shape.waveshaper.mix")
                storedMix = value;
        }

        /** The transfer curve itself, and its antiderivative. */
        static double shape (Curve c, double x) noexcept
        {
            switch (c)
            {
                case Curve::Tanh:       return std::tanh (x);
                case Curve::Cubic:      return std::abs (x) <= 1.0 ? x - x * x * x / 3.0 : (x > 0 ? 2.0 / 3.0 : -2.0 / 3.0);
                case Curve::Hard:       return juce::jlimit (-1.0, 1.0, x);
                case Curve::Algebraic:  return x / std::sqrt (1.0 + x * x);
                case Curve::Asymmetric: return x >= 0.0 ? std::tanh (x) : 2.0 * std::tanh (0.5 * x);
                case Curve::Sine:       return std::sin (x);
            }
            return x;
        }

        static double antiderivative (Curve c, double x) noexcept
        {
            switch (c)
            {
                case Curve::Tanh:       return adaa::logCosh (x);
                case Curve::Cubic:      return std::abs (x) <= 1.0 ? 0.5 * x * x - x * x * x * x / 12.0 : 2.0 / 3.0 * std::abs (x) - 0.25;
                case Curve::Hard:       return std::abs (x) <= 1.0 ? 0.5 * x * x : std::abs (x) - 0.5;
                case Curve::Algebraic:  return std::sqrt (1.0 + x * x);
                case Curve::Asymmetric: return x >= 0.0 ? adaa::logCosh (x) : 4.0 * adaa::logCosh (0.5 * x);
                case Curve::Sine:       return -std::cos (x);
            }
            return 0.5 * x * x;
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto driveDb = std::isnan (inputs[1]) ? storedDrive : juce::jlimit (0.0f, 36.0f, inputs[1]);
            const auto bias = (double) (std::isnan (inputs[2]) ? storedBias : juce::jlimit (-1.0f, 1.0f, inputs[2]));
            const auto outputDb = std::isnan (inputs[3]) ? storedOutput : juce::jlimit (-24.0f, 12.0f, inputs[3]);
            const auto mix = std::isnan (inputs[4]) ? storedMix : juce::jlimit (0.0f, 1.0f, inputs[4]);

            const auto x = (double) inputs[0] * juce::Decibels::decibelsToGain ((double) driveDb) + bias;
            const auto c = curve;
            const auto wet = adaa::firstOrder (x, previousX, [c] (double v) { return shape (c, v); },
                                               [c] (double v) { return antiderivative (c, v); })
                             - shape (c, bias);
            previousX = x;

            const auto gain = juce::Decibels::decibelsToGain ((double) outputDb);
            outputs[0] = (float) ((double) inputs[0] * (1.0 - mix) + wet * gain * mix);
        }

    private:
        Curve curve = Curve::Tanh;
        double previousX = 0.0;
        float storedDrive = 6.0f, storedBias = 0.0f, storedOutput = 0.0f, storedMix = 1.0f;
    };

    //==============================================================================
    /** Stable type id: "shape.fold". A wavefolder: past ±1 the signal folds
        back instead of clipping, so more `fold` gain adds more and more
        partials (West-coast timbre). Triangle folding (straight reflections)
        or sine folding (rounder), both antialiased (ADAA). `bias` folds
        asymmetrically; its static DC is removed. */
    class ShapeFoldNode : public Node
    {
    public:
        enum class Shape { Triangle, Sine };

        void reset() override { previousX = 0.0; }

        int getNumInputPorts() const noexcept override { return 4; }
        int getNumOutputPorts() const noexcept override { return 1; }
        juce::String getTitle() const override { return "Fold"; }
        juce::String getCategory() const override { return "Shape"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return {
                perChannel ({ .id = "in", .type = SignalType::Signal, .quantity = Quantity::Audio }),
                PortDescriptor { .id = "shape.fold.fold", .type = SignalType::Signal, .label = "Fold",
                                  .minValue = 1.0f, .maxValue = 20.0f, .defaultValue = 2.0f, .isLogScale = true,
                                  .hasFallbackWhenUnconnected = true, .curve = bazalt::engine::Curve::Logarithmic },
                PortDescriptor { .id = "shape.fold.bias", .type = SignalType::Signal, .label = "Bias",
                                  .minValue = -1.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .hasFallbackWhenUnconnected = true,
                                  .quantity = Quantity::Bipolar, .polarity = Polarity::Bipolar },
                PortDescriptor { .id = "shape.fold.mix", .type = SignalType::Signal, .label = "Mix",
                                  .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .hasFallbackWhenUnconnected = true,
                                  .quantity = Quantity::Unipolar },
            };
        }
        std::vector<PortDescriptor> getOutputPorts() const override
        {
            return { perChannel (PortDescriptor { .id = "out", .type = SignalType::Signal, .label = "Out", .isPrimaryOutput = true, .quantity = Quantity::Audio }) };
        }
        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "shape.fold.shape", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                            .displayName = "Shape", .isInteger = true, .kind = ValueKind::Enum,
                                            .enumOptions = { { "triangle", "Triangle" }, { "sine", "Sine" } } } };
        }
        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "shape.fold.shape")
                shapeKind = value > 0.5f ? Shape::Sine : Shape::Triangle;
            else if (parameterId == "shape.fold.fold")
                storedFold = value;
            else if (parameterId == "shape.fold.bias")
                storedBias = value;
            else if (parameterId == "shape.fold.mix")
                storedMix = value;
        }

        /** Triangle fold, period 4: 0 at 0, +1 at 1, 0 at 2, -1 at 3. */
        static double triangle (double x) noexcept
        {
            const auto m = positiveMod (x + 1.0, 4.0);
            return 1.0 - std::abs (m - 2.0);
        }
        static double triangleAntiderivative (double x) noexcept
        {
            const auto m = positiveMod (x + 1.0, 4.0);
            return m <= 2.0 ? 0.5 * m * m - m : -0.5 * m * m + 3.0 * m - 4.0;
        }

        static double fold (Shape s, double x) noexcept
        {
            return s == Shape::Triangle ? triangle (x) : std::sin (juce::MathConstants<double>::halfPi * x);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto gain = (double) (std::isnan (inputs[1]) ? storedFold : juce::jlimit (1.0f, 20.0f, inputs[1]));
            const auto bias = (double) (std::isnan (inputs[2]) ? storedBias : juce::jlimit (-1.0f, 1.0f, inputs[2]));
            const auto mix = std::isnan (inputs[3]) ? storedMix : juce::jlimit (0.0f, 1.0f, inputs[3]);

            const auto x = (double) inputs[0] * gain + bias;
            const auto s = shapeKind;
            const auto wet = adaa::firstOrder (x, previousX, [s] (double v) { return fold (s, v); },
                                               [s] (double v)
                                               {
                                                   return s == Shape::Triangle ? triangleAntiderivative (v)
                                                                               : -std::cos (juce::MathConstants<double>::halfPi * v) / juce::MathConstants<double>::halfPi;
                                               })
                             - fold (s, bias);
            previousX = x;
            outputs[0] = (float) ((double) inputs[0] * (1.0 - mix) + wet * mix);
        }

    private:
        static double positiveMod (double x, double m) noexcept { return x - m * std::floor (x / m); }

        Shape shapeKind = Shape::Triangle;
        double previousX = 0.0;
        float storedFold = 2.0f, storedBias = 0.0f, storedMix = 1.0f;
    };
}
