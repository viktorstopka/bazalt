// wiki/plans/Sampling.md 3a.1: a sample as Data — the region a Sample node
// publishes, the store it reads from, and a missing sample.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/graph/SampleData.h"
#include "bazalt/engine/nodes/DataSampleNode.h"

using namespace bazalt::engine;

namespace
{
    /** A 1 s stereo ramp at 1 kHz: left = frame, right = -frame. */
    std::shared_ptr<SampleSource> ramp (const juce::String& id)
    {
        auto s = std::make_shared<SampleSource>();
        s->id = id;
        s->name = "ramp.wav";
        s->channels = 2;
        s->frames = 1000;
        s->sampleRate = 1000.0;
        for (int f = 0; f < s->frames; ++f)
        {
            s->data.push_back ((float) f);
            s->data.push_back ((float) -f);
        }
        return s;
    }

    juce::var content (const juce::String& json) { return juce::JSON::parse (json); }
}

TEST_CASE ("A sample id is a content hash", "[engine][sample]")
{
    const char a[] = "RIFF....WAVE";
    const char b[] = "RIFF....WAVf";
    CHECK (sampleIdForBytes (a, sizeof (a)) == sampleIdForBytes (a, sizeof (a)));
    CHECK (sampleIdForBytes (a, sizeof (a)) != sampleIdForBytes (b, sizeof (b)));
    CHECK (sampleIdForBytes (a, sizeof (a)).length() == 16);
}

TEST_CASE ("A region trims, reverses, fades and marks loops and slices, without touching the source", "[engine][sample]")
{
    const auto source = ramp ("r1");
    auto region = SampleRegion::fromVar (content (R"({ "sample": "r1", "start": 0.1, "end": 0.2, "loop": true,
        "loopStart": 0.02, "loopEnd": 0.08, "slices": [0.05, 0.03] })"));
    auto buffer = buildSampleBuffer (source.get(), region, (uint8_t) Quantity::Audio);
    REQUIRE (buffer->tag() == DataTag::Sample);
    CHECK (buffer->channels() == 2);
    CHECK (buffer->frames() == 100);
    CHECK (buffer->sampleRate() == 1000.0);
    CHECK (buffer->at (0, 0) == 100.0f);
    CHECK (buffer->at (99, 1) == -199.0f);
    CHECK (buffer->sampleMeta.loopStart == 20);
    CHECK (buffer->sampleMeta.loopEnd == 80);
    CHECK (buffer->sampleMeta.slices == std::vector<int> { 30, 50 });
    CHECK (buffer->sampleMeta.channelQuantities == std::vector<uint8_t> { (uint8_t) Quantity::Audio, (uint8_t) Quantity::Audio });

    region.reverse = true;
    region.loop = false;
    buffer = buildSampleBuffer (source.get(), region, (uint8_t) Quantity::Audio);
    CHECK (buffer->at (0, 0) == 199.0f);
    CHECK (buffer->sampleMeta.loopStart == -1);

    region.reverse = false;
    region.normalise = true;
    region.fadeIn = 0.01;
    buffer = buildSampleBuffer (source.get(), region, (uint8_t) Quantity::Audio);
    CHECK (buffer->at (0, 0) == 0.0f);                          // faded in
    CHECK (buffer->at (99, 0) == Catch::Approx (1.0f));          // normalised peak
    CHECK (source->at (150, 0) == 150.0f);                       // the source is untouched
}

TEST_CASE ("The Sample node publishes its region, reports a missing sample, and recovers when it arrives",
           "[engine][sample]")
{
    SampleStore::instance().clearForTesting();
    nodes::DataSampleNode node;
    node.prepare ({ 48000.0, 64 });
    REQUIRE (node.setContent (content (R"({ "sample": "r2", "name": "ramp.wav", "end": 0.5 })")));
    CHECK (node.isMissing());
    const auto* silent = node.getDataPublisher()->getCurrentForAudioThread();
    REQUIRE (silent != nullptr);
    CHECK (silent->frames() == 1);
    CHECK (silent->at (0) == 0.0f);

    SampleStore::instance().add (ramp ("r2"));
    REQUIRE (node.setContent (node.getRegion().toVar())); // what GraphEditController::refreshSamples does
    CHECK_FALSE (node.isMissing());
    const auto* found = node.getDataPublisher()->getCurrentForAudioThread();
    REQUIRE (found != nullptr);
    CHECK (found->frames() == 500);

    std::vector<float> length (64);
    float* outs[] = { nullptr, length.data() };
    node.processBlock (nullptr, outs, 64);
    CHECK (length[10] == Catch::Approx (0.5f));
    SampleStore::instance().clearForTesting();
}

#include "bazalt/engine/graph/SampleAnalysis.h"

TEST_CASE ("The Sample factory hears a sample's note and its attacks", "[engine][sample][3a]")
{
    const auto sr = 48000.0;
    SampleSource tone;
    tone.sampleRate = sr;
    tone.frames = (int) sr; // 1 s of A3 (MIDI 57, 220 Hz)
    for (int f = 0; f < tone.frames; ++f)
        tone.data.push_back ((float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * f / sr) * 0.5f);
    CHECK (sampleAnalysis::rootPitch (tone) == Catch::Approx (57.0f).margin (0.1f));

    SampleSource silence;
    silence.sampleRate = sr;
    silence.frames = 4800;
    silence.data.assign (4800, 0.0f);
    CHECK (std::isnan (sampleAnalysis::rootPitch (silence)));

    // Four decaying noise bursts at 0.1, 0.4, 0.7 and 1.0 s.
    SampleSource hits;
    hits.sampleRate = sr;
    hits.frames = (int) (1.3 * sr);
    hits.data.assign ((size_t) hits.frames, 0.0f);
    juce::Random random (7);
    for (const auto at : { 0.1, 0.4, 0.7, 1.0 })
        for (int i = 0; i < (int) (0.08 * sr); ++i)
            hits.data[(size_t) (at * sr) + (size_t) i] = (random.nextFloat() * 2.0f - 1.0f) * std::exp (-(float) i / (0.01f * (float) sr));
    const auto times = sampleAnalysis::transients (hits);
    REQUIRE (times.size() == 4);
    for (size_t i = 0; i < 4; ++i)
        CHECK (times[i] == Catch::Approx (0.1 + 0.3 * (double) i).margin (0.01));
}
