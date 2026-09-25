#pragma once

#include "bazalt/engine/nodes/InheritingPortsNode.h"
#include <algorithm>
#include <cmath>

namespace bazalt::engine::nodes
{
    /** The three placeable viewers (NODE_CATALOG.md's `view.*` row): each has
        ONE input, no outputs, and does nothing to the signal - a viewer is a
        legitimate dead end the compiler schedules like any node
        (`view.listen` is the same shape). What it shows is a preview declared
        on its own input port (`getPreviews()`), which the shared M20 machinery
        renders; the engine resolves a preview on an input to the buffer wired
        into it (ADR-0029), so a viewer costs nothing on the audio thread
        beyond the tap's own copy, and only while it is on screen.

        Like every preview it is only live while the node is near the
        viewport, and an unwired input simply shows nothing yet: the
        subscription is remembered and binds the moment a cable is connected.

        The settings are this node's parameters, and `getPreviews()` builds its
        declaration from their CURRENT values. That is the whole mechanism
        (ADR-0029): the processor reads the live node's declaration whenever it
        attaches a tap, and every edit - including editing one of these
        parameters - recompiles and re-attaches, so `AnalysisThread` always
        applies what the node currently says. Nothing about the settings is
        known to the UI beyond the parameters it already renders.

        Settings that are deliberately not offered yet (ADR-0029, Q1/Q2): a
        scope window longer than the tap ring (8192 samples: ~186 ms at
        44.1 kHz, shorter at higher rates), the per-note scope trigger, and
        the histogram meter mode.
    */

    /** "view.scope": a Scope. Takes any plain per-sample signal - Audio and
        Control as the catalog says, and also Boolean/Event, since watching a
        gate is one of the things a scope is for - through the same inherited-
        port mechanism `util.reroute` and `logic.select` use
        (InheritingPortsNode.h, ADR-0027). It does not take Note, Data or
        Spectral signals; those are rejected as an ordinary type mismatch.

        `timeWindow` is how much signal one screen shows, up to the tap ring's
        capacity. `trigger` is `Free` (always the newest window) or `Rising
        edge` (start on the latest rising crossing of the signal's mid-level,
        which holds a periodic waveform still).
    */
    class ViewScopeNode : public InheritingPortsNode
    {
    public:
        ViewScopeNode() noexcept : InheritingPortsNode (SignalType::Audio) {}

        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 0; }

        juce::String getTitle() const override { return "Scope"; }
        juce::String getCategory() const override { return "View"; }
        NodeLayoutVariant getLayoutVariant() const override { return NodeLayoutVariant::Horizontal; }

        void resolveIncomingPort (const juce::String& toPortId, const PortDescriptor& source) noexcept override
        {
            if (toPortId == "in")
                offer (0, source, true);
        }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in", .type = resolvedType, .label = "In", .quantity = resolvedQuantity,
                                       .polymorphism = PortPolymorphism::SignalAndQuantity } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "view.scope.timeWindow",
                                       .minValue = 1.0f,
                                       .maxValue = 150.0f,
                                       .defaultValue = 50.0f,
                                       .skew = 0.4f,
                                       .unit = "ms",
                                       .displayName = "Time Window",
                                       .quantity = Quantity::Time,
                                       .curve = Curve::Logarithmic,
                                       .isStructural = true },
                ParameterDescriptor { .id = "view.scope.trigger",
                                       .minValue = 0.0f,
                                       .maxValue = 1.0f,
                                       .defaultValue = 0.0f,
                                       .displayName = "Trigger",
                                       .isInteger = true,
                                       .kind = ValueKind::Enum,
                                       .enumOptions = { { "free", "Free" }, { "risingEdge", "Rising Edge" } },
                                       .isStructural = true },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "view.scope.timeWindow")
                timeWindowMs = std::clamp (value, 1.0f, 150.0f);
            else if (parameterId == "view.scope.trigger")
                trigger = std::lround (value) == 1 ? ScopeTriggerMode::RisingEdge : ScopeTriggerMode::Free;
        }

        std::vector<PreviewDescriptor> getPreviews() const override
        {
            return { PreviewDescriptor { .kind = PreviewKind::Waveform,
                                          .portId = "in",
                                          .timeWindowSeconds = timeWindowMs * 0.001f,
                                          .triggerMode = trigger } };
        }

        void processSample (const float*, float*) noexcept override {}

    private:
        float timeWindowMs = 50.0f;
        ScopeTriggerMode trigger = ScopeTriggerMode::Free;
    };

    /** "view.spectrum": a Spectrum analyser. Audio only - a spectrum of a
        control signal isn't something the analysis is meant for.

        `fftSize` (512-8192; bigger resolves finer frequencies but reacts
        slower), `tilt` (a display slope in dB/octave about 1 kHz, e.g. +3 makes
        pink noise look flat), `averaging` (smooths the display over time).
    */
    class ViewSpectrumNode : public Node
    {
    public:
        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 0; }

        juce::String getTitle() const override { return "Spectrum"; }
        juce::String getCategory() const override { return "View"; }
        NodeLayoutVariant getLayoutVariant() const override { return NodeLayoutVariant::Horizontal; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in", .type = SignalType::Audio, .label = "In" } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return {
                ParameterDescriptor { .id = "view.spectrum.fftSize",
                                       .minValue = 0.0f,
                                       .maxValue = 4.0f,
                                       .defaultValue = 2.0f, // 2048, what every spectrum was before it was a setting
                                       .displayName = "FFT Size",
                                       .isInteger = true,
                                       .kind = ValueKind::Enum,
                                       .enumOptions = { { "512", "512" }, { "1024", "1024" }, { "2048", "2048" },
                                                         { "4096", "4096" }, { "8192", "8192" } },
                                       .isStructural = true },
                ParameterDescriptor { .id = "view.spectrum.tilt",
                                       .minValue = -6.0f,
                                       .maxValue = 6.0f,
                                       .defaultValue = 0.0f,
                                       .unit = "dB/oct",
                                       .displayName = "Tilt",
                                       .isStructural = true },
                ParameterDescriptor { .id = "view.spectrum.averaging",
                                       .minValue = 0.0f,
                                       .maxValue = 0.95f,
                                       .defaultValue = 0.0f,
                                       .displayName = "Averaging",
                                       .isStructural = true },
            };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "view.spectrum.fftSize")
                fftSizeIndex = std::clamp ((int) std::lround (value), 0, 4);
            else if (parameterId == "view.spectrum.tilt")
                tilt = std::clamp (value, -6.0f, 6.0f);
            else if (parameterId == "view.spectrum.averaging")
                averaging = std::clamp (value, 0.0f, 0.95f);
        }

        std::vector<PreviewDescriptor> getPreviews() const override
        {
            return { PreviewDescriptor { .kind = PreviewKind::Spectrum,
                                          .portId = "in",
                                          .fftSize = 512 << fftSizeIndex,
                                          .tiltDbPerOctave = tilt,
                                          .averaging = averaging } };
        }

        void processSample (const float*, float*) noexcept override {}

    private:
        int fftSizeIndex = 2;
        float tilt = 0.0f;
        float averaging = 0.0f;
    };

    /** "view.meter": a level Meter, for Audio or any other plain signal (same
        input rules as view.scope). `mode` is `Peak` (a fast-attack peak line
        over an RMS bar), `RMS` (one reading: the line rides the bar) or `True
        Peak` (the peak line taken from the signal reconstructed between its
        samples at 4x by a 16-tap windowed sinc - an estimate that lands within a
        fraction of a dB on ordinary material, not the ITU-R BS.1770 measurement).
    */
    class ViewMeterNode : public InheritingPortsNode
    {
    public:
        ViewMeterNode() noexcept : InheritingPortsNode (SignalType::Audio) {}

        int getNumInputPorts() const noexcept override { return 1; }
        int getNumOutputPorts() const noexcept override { return 0; }

        juce::String getTitle() const override { return "Meter"; }
        juce::String getCategory() const override { return "View"; }
        NodeLayoutVariant getLayoutVariant() const override { return NodeLayoutVariant::Horizontal; }

        void resolveIncomingPort (const juce::String& toPortId, const PortDescriptor& source) noexcept override
        {
            if (toPortId == "in")
                offer (0, source, true);
        }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            return { PortDescriptor { .id = "in", .type = resolvedType, .label = "In", .quantity = resolvedQuantity,
                                       .polymorphism = PortPolymorphism::SignalAndQuantity } };
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            return { ParameterDescriptor { .id = "view.meter.mode",
                                            .minValue = 0.0f,
                                            .maxValue = 2.0f,
                                            .defaultValue = 0.0f,
                                            .displayName = "Mode",
                                            .isInteger = true,
                                            .kind = ValueKind::Enum,
                                            .enumOptions = { { "peak", "Peak" }, { "rms", "RMS" }, { "truePeak", "True Peak (4x)" } },
                                            .isStructural = true } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            if (parameterId == "view.meter.mode")
            {
                switch (std::clamp ((int) std::lround (value), 0, 2))
                {
                    case 1:  mode = MeterMode::Rms; break;
                    case 2:  mode = MeterMode::TruePeak; break;
                    default: mode = MeterMode::Peak; break;
                }
            }
        }

        std::vector<PreviewDescriptor> getPreviews() const override
        {
            return { PreviewDescriptor { .kind = PreviewKind::Meter, .portId = "in", .meterMode = mode } };
        }

        void processSample (const float*, float*) noexcept override {}

    private:
        MeterMode mode = MeterMode::Peak;
    };
}
