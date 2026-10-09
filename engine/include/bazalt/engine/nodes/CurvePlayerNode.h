#pragma once

#include "bazalt/engine/graph/CurveData.h"
#include "bazalt/engine/graph/HostInputs.h"
#include "bazalt/engine/graph/WavetableData.h"
#include "bazalt/engine/telemetry/PhaseSnapshot.h"
#include "bazalt/engine/graph/ValueTypes.h"
#include "bazalt/engine/nodes/DataCurveNode.h"
#include <atomic>
#include <cmath>
#include <vector>

namespace bazalt::engine::nodes
{
    /** Stable type ids: "source.oscillator" and "source.envelope" — one node
        that plays a curve (wiki/plans/DataAndWavetable.md D5/D6), two Add-menu
        entries so both are easy to find.

        **Shape** is a Data(curve) input. Wired, it plays what is plugged in (a
        data.curve shared by several nodes, or — Oscillator only — a
        data.wavetable, played at the frame its Frame input says); unwired, it
        plays the node's own curve (its content, edited in the Factory window)
        — the same "I have a value, but you can connect me" rule every other
        input follows, applied to a shape. A shape change (an edit, or a new
        cable) never clicks: the jump is spread over ~5 ms.

        **Oscillator** (Cycle time base): the curve is one period, played at
        Frequency, band-limited — CurveView's mipmap level for the frequency,
        and the curve exactly as drawn below ~20 Hz, so the same node is a
        clean LFO and an alias-free oscillator. Trigger restarts the cycle
        (hard sync); with Loop off it plays one cycle per trigger and holds.
        Phase offsets the read, never the running phase. **Sync** ties the
        cycle to the host tempo instead of Frequency (Division: 8 bars …
        1/32), and while the host plays the phase IS the host position — it
        lands on the same point of the cycle at the same bar every time (what
        lfo.shape did).

        **Envelope** (Time base, x in seconds): a rising Gate starts the curve
        from wherever the output is (no click on a retrigger); it holds at the
        point marked S while the gate is high, and on release continues from
        S to the end, starting from the current level. No S marker: the whole
        curve plays on each rising gate (a one-shot). A loop range repeats
        while the gate is held. Time Scale stretches the whole curve.

        The Oscillator is a phase source (Node::isPhaseSource), so view.cycle
        and every phase-locked preview downstream fold their real samples by
        its phase. Its snapshot carries no render function — a drawn curve
        does not fit one — so its previews fold rather than render.

        Sample-rate handling (CLAUDE.md rule 6): increments derive from the
        sample rate set in prepare(); nothing depends on the block size.
    */
    class CurvePlayerNode : public Node
    {
    public:
        enum class Mode { Oscillator, Envelope };

        static constexpr float defaultFrequencyHz = 440.0f;
        static constexpr double declickSeconds = 0.005;

        explicit CurvePlayerNode (Mode modeToUse)
            : mode (modeToUse),
              prefix (modeToUse == Mode::Oscillator ? "source.oscillator" : "source.envelope"),
              frequencyId (prefix + ".frequency"),
              amplitudeId (prefix + ".amplitude"),
              phaseId (prefix + ".phase"),
              loopId (prefix + ".loop"),
              timeScaleId (prefix + ".timeScale"),
              syncId (prefix + ".sync"),
              divisionId (prefix + ".division"),
              ownCurve (modeToUse == Mode::Oscillator ? CurveDocument::TimeBase::Cycle : CurveDocument::TimeBase::Time)
        {
        }

        void prepare (const NodePrepareInfo& info) override
        {
            sampleRate = info.sampleRate;
            declickCoeff = (float) std::exp (-1.0 / (declickSeconds * sampleRate));
            phaseTrack.assign ((size_t) std::max (1, info.maxBlockSize), 0.0f);
            ownCurve.ensurePublished();
        }

        void reset() override
        {
            phase = 0.0;
            cycleCount = 0;
            oneShotDone = false;
            stage = Stage::Idle;
            finished = false;
            previousGate = false; // a reset voice must see its next gate as a rising edge
            time = 0.0;
            output = 0.0f;
            offset = 0.0f;
            lastBuffer = nullptr;
        }

        int getNumInputPorts() const noexcept override { return mode == Mode::Oscillator ? 6 : 4; }

        bool wantsHostInputs() const noexcept override { return mode == Mode::Oscillator; }

        // ---- Phase source (fold-only: PhaseSnapshot::render stays null) -----
        bool isPhaseSource() const noexcept override { return mode == Mode::Oscillator; }
        const float* getPhaseTrack() const noexcept override { return phaseTrack.data(); }
        void capturePhaseSnapshot (PhaseSnapshot& snapshot) const noexcept override
        {
            snapshot.render = nullptr;
            snapshot.frequencyHz = lastFrequency;
            snapshot.sampleRate = sampleRate;
            snapshot.playhead = (float) ((double) cycleCount + phase);
        }
        void setHostInputs (const HostInputs& inputs) noexcept override
        {
            tempoBpm = inputs.tempoBpm > 0.0 ? inputs.tempoBpm : 120.0;
            hostPlaying = inputs.transportPlaying;
            hostPpq = inputs.ppqPosition;
            hostPositionFresh = true;
        }

        std::vector<ParameterDescriptor> getParameters() const override
        {
            if (mode != Mode::Oscillator)
                return {};
            return {
                ParameterDescriptor { .id = syncId, .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                      .displayName = "Sync", .isInteger = true, .kind = ValueKind::Bool },
                ParameterDescriptor { .id = divisionId, .minValue = 0.0f, .maxValue = 8.0f, .defaultValue = 4.0f,
                                      .displayName = "Division", .isInteger = true, .kind = ValueKind::Enum,
                                      .enumOptions = { { "8bars", "8 bars" }, { "4bars", "4 bars" }, { "2bars", "2 bars" },
                                                       { "1bar", "1 bar" }, { "1/2", "1/2" }, { "1/4", "1/4" },
                                                       { "1/8", "1/8" }, { "1/16", "1/16" }, { "1/32", "1/32" } } },
            };
        }
        int getNumOutputPorts() const noexcept override { return 1; }

        juce::String getTitle() const override { return mode == Mode::Oscillator ? "Oscillator" : "Envelope"; }
        juce::String getCategory() const override { return "Sources"; }

        std::vector<PortDescriptor> getInputPorts() const override
        {
            auto shape = PortDescriptor { .id = "shape", .type = SignalType::Data, .label = "Shape",
                                          .dataTags = { DataTag::Curve } };
            if (mode == Mode::Oscillator)
                shape.dataTags.push_back (DataTag::Wavetable);
            const auto amplitude = PortDescriptor { .id = amplitudeId, .type = SignalType::Signal, .label = "Amplitude",
                                                    .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f,
                                                    .hasFallbackWhenUnconnected = true };
            if (mode == Mode::Oscillator)
                return {
                    shape,
                    PortDescriptor { .id = frequencyId, .type = SignalType::Signal, .label = "Frequency",
                                     .unit = "Hz", .minValue = 0.01f, .maxValue = 20000.0f, .defaultValue = defaultFrequencyHz,
                                     .isLogScale = true, .hasFallbackWhenUnconnected = true, .quantity = Quantity::Frequency,
                                     .curve = Curve::Logarithmic },
                    amplitude,
                    PortDescriptor { .id = phaseId, .type = SignalType::Signal, .label = "Phase",
                                     .minValue = -1.0f, .maxValue = 1.0f, .defaultValue = 0.0f,
                                     .hasFallbackWhenUnconnected = true, .quantity = Quantity::Bipolar,
                                     .polarity = Polarity::Bipolar },
                    PortDescriptor { .id = "trigger", .type = SignalType::Event, .label = "Trigger" },
                    PortDescriptor { .id = loopId, .type = SignalType::Signal, .label = "Loop",
                                     .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f,
                                     .hasFallbackWhenUnconnected = true, .kind = ValueKind::Bool, .quantity = Quantity::Boolean },
                };
            return {
                shape,
                PortDescriptor { .id = "gate", .type = SignalType::Signal, .label = "Gate",
                                 .hasFallbackWhenUnconnected = true, .kind = ValueKind::Bool, .quantity = Quantity::Boolean },
                amplitude,
                PortDescriptor { .id = timeScaleId, .type = SignalType::Signal, .label = "Time Scale",
                                 .minValue = 0.05f, .maxValue = 20.0f, .defaultValue = 1.0f,
                                 .isLogScale = true, .hasFallbackWhenUnconnected = true, .quantity = Quantity::Ratio,
                                 .curve = Curve::Logarithmic },
            };
        }

        std::vector<PortDescriptor> getOutputPorts() const override
        {
            if (mode == Mode::Oscillator)
                return { PortDescriptor { .id = "out", .type = SignalType::Signal, .label = "Out", .isPrimaryOutput = true,
                                          .quantity = Quantity::Audio } };
            return { PortDescriptor { .id = "out", .type = SignalType::Signal, .label = "Out", .isPrimaryOutput = true,
                                      .minValue = 0.0f, .maxValue = 1.0f, .quantity = Quantity::Unipolar } };
        }

        void setParameter (const juce::String& parameterId, float value) override
        {
            // Ids built once in the constructor: this may run on the audio
            // thread (macro automation), which must not allocate.
            if (parameterId == frequencyId)
                storedFrequency = value;
            else if (parameterId == amplitudeId)
                storedAmplitude = value;
            else if (parameterId == phaseId)
                storedPhase = value;
            else if (parameterId == loopId)
                storedLoop = value;
            else if (parameterId == timeScaleId)
                storedTimeScale = value;
            else if (parameterId == syncId)
                synced = value > 0.5f;
            else if (parameterId == divisionId)
            {
                constexpr double beats[] = { 32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 };
                beatsPerCycle = beats[juce::jlimit (0, 8, (int) std::lround (value))];
            }
        }

        bool setContent (const juce::var& content) override { return ownCurve.setContent (content); }

        void setDataInput (const juce::String& inputPortId, DataPublisher* publisher) noexcept override
        {
            if (inputPortId == "shape")
                shapeInput.store (publisher, std::memory_order_release);
        }

        void processBlock (const float* const* inputs, float* const* outputs, int numSamples) noexcept override
        {
            auto* wired = shapeInput.load (std::memory_order_acquire);
            const auto* buffer = (wired != nullptr ? wired : &ownCurve.getPublisher())->getCurrentForAudioThread();
            if (buffer != lastBuffer)
            {
                declickPending = lastBuffer != nullptr; // the first buffer ever needs no smoothing
                lastBuffer = buffer;
            }
            view = CurveView (buffer);
            wavetable = WavetableView (buffer);
            framesFrom = wired;
            trackIndex = 0;
            sampleIndex = 0;
            Node::processBlock (inputs, outputs, numSamples);
        }

        void processSample (const float* inputs, float* outputs) noexcept override
        {
            const auto raw = mode == Mode::Oscillator ? oscillatorSample (inputs) : envelopeSample (inputs);

            if (declickPending)
            {
                offset = output - raw;
                declickPending = false;
            }
            output = raw + offset;
            offset *= declickCoeff;
            outputs[0] = output;
            ++sampleIndex;
        }

        bool isActive() const noexcept { return mode == Mode::Oscillator || stage != Stage::Idle; }

    private:
        enum class Stage { Idle, Running, Holding, Releasing };

        static float read (const float* inputs, int index, float stored) noexcept
        {
            return std::isnan (inputs[index]) ? stored : inputs[index];
        }

        float oscillatorSample (const float* inputs) noexcept
        {
            const auto frequency = synced ? (float) (tempoBpm / 60.0 / beatsPerCycle) : read (inputs, 1, storedFrequency);
            const auto amplitude = read (inputs, 2, storedAmplitude);
            const auto phaseOffset = read (inputs, 3, storedPhase);
            const auto loop = read (inputs, 5, storedLoop) > 0.5f;

            if (std::fabs (inputs[4]) > 0.0f && ! std::isnan (inputs[4]))
            {
                phase = 0.0;
                oneShotDone = false;
            }
            else if (synced && hostPlaying && hostPositionFresh)
            {
                // Locked to the timeline: the cycle position IS the host position.
                const auto cycles = hostPpq / beatsPerCycle;
                phase = cycles - std::floor (cycles);
            }
            hostPositionFresh = false;

            const auto increment = sampleRate > 0.0 ? (double) frequency / sampleRate : 0.0;
            lastFrequency = frequency;
            if (trackIndex < phaseTrack.size())
                phaseTrack[trackIndex++] = (float) ((double) cycleCount + phase);
            float value = 0.0f;
            if (wavetable.isValid())
            {
                const auto position = oneShotDone ? 1.0 - 1.0e-9 : phase + (double) phaseOffset;
                const auto frame = framesFrom != nullptr ? framesFrom->readCompanion (sampleIndex, 0.0f) : 0.0f;
                value = wavetable.read (CurveView::levelFor (increment), position, frame);
            }
            else if (view.isValid())
            {
                const auto position = oneShotDone ? 1.0 - 1.0e-9 : phase + (double) phaseOffset;
                value = view.hasBandLimitedTables() ? view.read (CurveView::levelFor (increment), position)
                                                    : view.evaluate ((float) (position - std::floor (position)));
            }

            if (! oneShotDone)
            {
                phase += increment;
                if (phase >= 1.0 || phase < 0.0)
                {
                    if (! loop && phase >= 1.0)
                        oneShotDone = true;
                    const auto wraps = (long long) std::floor (phase);
                    cycleCount = (int) ((((long long) cycleCount + wraps) % phaseLockedCycles + phaseLockedCycles) % phaseLockedCycles);
                    phase -= std::floor (phase);
                }
            }
            return value * amplitude;
        }

        float envelopeSample (const float* inputs) noexcept
        {
            const auto gate = ! std::isnan (inputs[1]) && inputs[1] > 0.5f;
            const auto amplitude = read (inputs, 2, storedAmplitude);
            const auto timeScale = juce::jmax (0.001f, read (inputs, 3, storedTimeScale));

            if (! view.isValid() || view.getNumPoints() == 0)
            {
                previousGate = gate;
                return 0.0f;
            }

            const auto count = view.getNumPoints();
            const auto end = (double) view.point (count - 1).x;
            const auto sustain = view.sustainIndex();
            const auto sustainX = sustain >= 0 ? (double) view.point (sustain).x : -1.0;
            const auto level = amplitude > 1.0e-6f ? output / amplitude : 0.0f;

            if (gate && ! previousGate)
            {
                // Start from wherever the output is: the gap closes over the first segment.
                time = 0.0;
                stage = Stage::Running;
                finished = false;
                startGap = level - view.point (0).y;
                startGapEnd = count > 1 ? juce::jmax (1.0e-6, (double) view.point (1).x) : 1.0e-6;
            }
            else if (! gate && previousGate && sustain >= 0 && (stage == Stage::Running || stage == Stage::Holding))
            {
                // Release from the current level: the gap closes by the end.
                releaseGap = level - view.evaluate ((float) sustainX);
                time = juce::jmax (time, sustainX);
                releaseFrom = time;
                stage = Stage::Releasing;
                startGap = 0.0f;
            }
            previousGate = gate;

            if (stage == Stage::Idle)
                return view.point (finished ? count - 1 : 0).y * amplitude;

            auto value = view.evaluate ((float) time);
            if (stage == Stage::Running && time < startGapEnd)
                value += startGap * (float) (1.0 - time / startGapEnd);
            if (stage == Stage::Releasing && end > releaseFrom)
                value += releaseGap * (float) juce::jmax (0.0, 1.0 - (time - releaseFrom) / (end - releaseFrom));

            if (stage != Stage::Holding)
            {
                time += 1.0 / (sampleRate * (double) timeScale);

                const auto loopStart = (double) view.loopStart();
                const auto loopEnd = (double) view.loopEnd();
                if (stage == Stage::Running && gate && loopStart >= 0.0 && loopEnd > loopStart && time >= loopEnd)
                    time = loopStart + std::fmod (time - loopStart, loopEnd - loopStart);
                else if (stage == Stage::Running && sustain >= 0 && time >= sustainX)
                {
                    time = sustainX;
                    stage = gate ? Stage::Holding : Stage::Releasing;
                    releaseFrom = sustainX;
                    releaseGap = 0.0f;
                }
                else if (time >= end)
                {
                    time = end;
                    stage = Stage::Idle;
                    finished = true;
                }
            }
            return value * amplitude;
        }

        Mode mode;
        juce::String prefix, frequencyId, amplitudeId, phaseId, loopId, timeScaleId, syncId, divisionId;
        CurvePublisher ownCurve;
        std::atomic<DataPublisher*> shapeInput { nullptr };
        CurveView view { nullptr };
        WavetableView wavetable { nullptr };
        DataPublisher* framesFrom = nullptr; // the wired shape's publisher: a wavetable's Frame rides on it
        int sampleIndex = 0;
        const DataBuffer* lastBuffer = nullptr;

        double sampleRate = 44100.0;
        float declickCoeff = 0.0f;
        bool declickPending = false;
        float output = 0.0f, offset = 0.0f;

        // Oscillator
        double phase = 0.0;
        int cycleCount = 0; // completed cycles mod phaseLockedCycles — the preview's cycle index
        std::vector<float> phaseTrack;
        size_t trackIndex = 0;
        float lastFrequency = defaultFrequencyHz;
        bool oneShotDone = false;
        bool synced = false, hostPlaying = false, hostPositionFresh = false;
        double tempoBpm = 120.0, hostPpq = 0.0, beatsPerCycle = 2.0; // Division 1/2, the default

        // Envelope
        Stage stage = Stage::Idle;
        bool previousGate = false, finished = false;
        double time = 0.0, startGapEnd = 1.0, releaseFrom = 0.0;
        float startGap = 0.0f, releaseGap = 0.0f;

        float storedFrequency = defaultFrequencyHz, storedAmplitude = 1.0f, storedPhase = 0.0f,
              storedLoop = 1.0f, storedTimeScale = 1.0f;
    };

    struct CurveOscillatorNode : CurvePlayerNode { CurveOscillatorNode() : CurvePlayerNode (Mode::Oscillator) {} };
    struct CurveEnvelopeNode : CurvePlayerNode { CurveEnvelopeNode() : CurvePlayerNode (Mode::Envelope) {} };
}
