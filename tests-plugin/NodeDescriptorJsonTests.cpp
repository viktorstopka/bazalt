#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include "NodeDescriptorJson.h"
#include "bazalt/engine/graph/ProofGraphs.h"
#include "bazalt/engine/nodes/ConstantNode.h"
#include "bazalt/engine/nodes/OscillatorNode.h"
#include "bazalt/engine/nodes/RerouteNode.h"
#include "bazalt/engine/nodes/InstanceMixNode.h"
#include "bazalt/engine/nodes/ViewGlanceNode.h"

using namespace bazalt;
using namespace bazalt::engine;

namespace
{
    // M20: no real node declares a preview yet (that's a later step once
    // the render/tap-push side exists to back one) — a synthetic node is
    // the only way to exercise the full Node::getPreviews() ->
    // NodeDescriptor::previews -> JSON pipeline end to end today.
    class SyntheticPreviewNode : public Node
    {
    public:
        int getNumOutputPorts() const noexcept override { return 1; }
        std::vector<PortDescriptor> getOutputPorts() const override { return { { "out", SignalType::Audio } }; }
        void processSample (const float*, float* outputs) noexcept override { outputs[0] = 0.0f; }

        std::vector<PreviewDescriptor> getPreviews() const override
        {
            PreviewDescriptor preview;
            preview.kind = PreviewKind::Spectrum;
            preview.portId = "out";
            preview.fftSize = 4096;
            preview.tiltDbPerOctave = 3.0f;
            preview.averaging = 0.5f;
            return { preview };
        }
    };
}

// Verifies the JSON shape ui/src/graph/descriptorTypes.ts expects
// (NODE_EDITOR.md §3) — this is a pure data transform (no WebView, no
// PluginEditor construction needed), so it's tested directly at the
// var level rather than through a live native-function round trip.

TEST_CASE ("nodeDescriptorToVar serializes a node with an unbounded numeric output port",
           "[plugin][NodeDescriptorJson][NODE_EDITOR]")
{
    const auto descriptor = describeNode ("util.constant", nodes::ConstantNode {});
    const auto var = nodeDescriptorToVar (descriptor);

    REQUIRE (var.isObject());
    CHECK (var["typeId"].toString() == "util.constant");
    CHECK (var["title"].toString() == "Constant");
    CHECK (var["category"].toString() == "Utility");
    CHECK (var["layoutVariant"].toString() == "standard");

    const auto* outputs = var["outputs"].getArray();
    REQUIRE (outputs != nullptr);
    REQUIRE (outputs->size() == 1);
    const auto& out = (*outputs)[0];
    CHECK (out["id"].toString() == "out");
    CHECK (out["type"].toString() == "control");
    CHECK ((bool) out["isPrimaryOutput"]);
    // Unset optional<float> must serialize to a JS-visible null (isVoid),
    // never 0 — 0 is a legitimate bound, absence isn't the same fact.
    CHECK (out["minValue"].isVoid());
    CHECK (out["maxValue"].isVoid());

    const auto* parameters = var["parameters"].getArray();
    REQUIRE (parameters != nullptr);
    REQUIRE (parameters->size() == 1);
    CHECK ((*parameters)[0]["id"].toString() == "util.constant.value");
    CHECK ((float) (*parameters)[0]["minValue"] == -100000.0f);
}

TEST_CASE ("nodeDescriptorToVar reports the Decoration layout variant",
           "[plugin][NodeDescriptorJson][NODE_EDITOR]")
{
    const auto descriptor = describeNode ("util.reroute", nodes::RerouteNode {});
    const auto var = nodeDescriptorToVar (descriptor);
    CHECK (var["layoutVariant"].toString() == "decoration");
}

TEST_CASE ("nodeDescriptorToVar reports the Glance layout variant and its declared preview",
           "[plugin][NodeDescriptorJson][M0.6]")
{
    const auto descriptor = describeNode ("view.glance", nodes::ViewGlanceNode {});
    const auto var = nodeDescriptorToVar (descriptor);
    CHECK (var["layoutVariant"].toString() == "glance");

    const auto* previews = var["previews"].getArray();
    REQUIRE (previews != nullptr);
    REQUIRE (previews->size() == 1);
    CHECK ((*previews)[0]["portId"].toString() == "out");
}

TEST_CASE ("nodeDescriptorToVar serializes InstanceMixNode's port metadata intact",
           "[plugin][NodeDescriptorJson][M17]")
{
    const auto descriptor = describeNode ("instance.mix", nodes::InstanceMixNode {});
    const auto var = nodeDescriptorToVar (descriptor);
    CHECK (var["typeId"].toString() == "instance.mix");

    const auto* inputs = var["inputs"].getArray();
    REQUIRE (inputs != nullptr);
    REQUIRE (! inputs->isEmpty());
}

TEST_CASE ("nodeDescriptorToVar serializes osc.analog.shape's M14 enum metadata end to end",
           "[plugin][NodeDescriptorJson][M14]")
{
    const auto descriptor = describeNode ("osc.analog", nodes::OscillatorNode {});
    const auto var = nodeDescriptorToVar (descriptor);

    const auto* parameters = var["parameters"].getArray();
    REQUIRE (parameters != nullptr);
    const auto it = std::find_if (parameters->begin(), parameters->end(), [] (const juce::var& p)
                                   { return p["id"].toString() == "osc.analog.shape"; });
    REQUIRE (it != parameters->end());

    CHECK ((*it)["kind"].toString() == "enum");
    CHECK ((bool) (*it)["isStructural"]);
    const auto* options = (*it)["enumOptions"].getArray();
    REQUIRE (options != nullptr);
    REQUIRE (options->size() == 4);
    CHECK ((*options)[0]["id"].toString() == "sine");
    CHECK ((*options)[0]["label"].toString() == "Sine");
    CHECK ((*options)[3]["id"].toString() == "triangle");
}

TEST_CASE ("nodeDescriptorToVar serializes an empty previews[] for a node that declares none",
           "[plugin][NodeDescriptorJson][M20]")
{
    const auto descriptor = describeNode ("util.constant", nodes::ConstantNode {});
    const auto var = nodeDescriptorToVar (descriptor);
    const auto* previews = var["previews"].getArray();
    REQUIRE (previews != nullptr);
    CHECK (previews->isEmpty());
}

TEST_CASE ("nodeDescriptorToVar serializes a declared preview end to end",
           "[plugin][NodeDescriptorJson][M20]")
{
    const auto descriptor = describeNode ("test.syntheticPreview", SyntheticPreviewNode {});
    const auto var = nodeDescriptorToVar (descriptor);

    const auto* previews = var["previews"].getArray();
    REQUIRE (previews != nullptr);
    REQUIRE (previews->size() == 1);
    const auto& preview = (*previews)[0];
    CHECK (preview["kind"].toString() == "spectrum");
    CHECK (preview["portId"].toString() == "out");
    CHECK ((int) preview["fftSize"] == 4096);
    CHECK ((float) preview["tiltDbPerOctave"] == 3.0f);
    CHECK ((float) preview["averaging"] == 0.5f);
    // Fields belonging to other kinds still serialize with their defaults
    // — additive/flat, never an omitted key depending on which kind is
    // active (matches PortDescriptor/ParameterDescriptor's own convention).
    CHECK (preview["triggerMode"].toString() == "free");
    CHECK (preview["meterMode"].toString() == "peak");
}

TEST_CASE ("nodeDescriptorsToVar serializes every registered type exactly once",
           "[plugin][NodeDescriptorJson][NODE_EDITOR]")
{
    const auto factory = buildDefaultNodeFactory();
    const auto descriptors = factory.describeAll();
    const auto var = nodeDescriptorsToVar (descriptors);
    const auto* array = var.getArray();

    REQUIRE (array != nullptr);
    REQUIRE ((size_t) array->size() == descriptors.size());

    // Every entry must round-trip a real, non-empty typeId — a blank
    // typeId reaching the UI would render as an untitled Add-menu entry.
    for (const auto& entry : *array)
        CHECK (entry["typeId"].toString().isNotEmpty());
}

TEST_CASE ("nodeDescriptorToVar flags a polymorphic-port node, and only that one, so the UI can resolve its real types",
           "[plugin][NodeDescriptorJson][Reroute]")
{
    // Without this flag the UI predicts wires against Reroute's declared
    // default (Audio) and rejects a Control cable the engine accepts.
    const auto factory = buildDefaultNodeFactory();

    auto flagFor = [&] (const juce::String& typeId)
    {
        const auto node = factory.create (typeId);
        REQUIRE (node != nullptr);
        return nodeDescriptorToVar (describeNode (typeId, *node)).getProperty ("hasPolymorphicPorts", juce::var());
    };

    CHECK (flagFor ("util.reroute").isBool());
    CHECK ((bool) flagFor ("util.reroute"));
    CHECK (flagFor ("osc.analog").isBool());
    CHECK_FALSE ((bool) flagFor ("osc.analog"));
}

TEST_CASE ("nodeDescriptorToVar reports each port's polymorphism, so the UI adopts a type only where the engine does",
           "[plugin][NodeDescriptorJson][inheriting]")
{
    const auto factory = buildDefaultNodeFactory();

    auto polymorphismOf = [&] (const juce::String& typeId, const juce::String& direction, const juce::String& portId) -> juce::String
    {
        const auto node = factory.create (typeId);
        REQUIRE (node != nullptr);
        const auto descriptor = nodeDescriptorToVar (describeNode (typeId, *node));
        const auto* ports = descriptor.getProperty (juce::Identifier (direction), juce::var()).getArray();
        REQUIRE (ports != nullptr);
        for (const auto& port : *ports)
            if (port["id"].toString() == portId)
                return port["polymorphism"].toString();
        FAIL ("no " << direction << " port " << portId << " on " << typeId);
        return {};
    };

    // Reroute and select's data ports follow type AND quantity...
    CHECK (polymorphismOf ("util.reroute", "inputs", "in") == "signalAndQuantity");
    CHECK (polymorphismOf ("logic.select", "inputs", "whenTrue") == "signalAndQuantity");
    CHECK (polymorphismOf ("logic.select", "outputs", "out") == "signalAndQuantity");
    // ...but select's Boolean condition, on that same polymorphic node, stays fixed.
    CHECK (polymorphismOf ("logic.select", "inputs", "condition") == "none");

    // compare and sample&hold keep their type Control and follow only the quantity.
    CHECK (polymorphismOf ("logic.compare", "inputs", "a") == "quantity");
    CHECK (polymorphismOf ("adapt.sampleHold", "inputs", "in") == "quantity");
    CHECK (polymorphismOf ("adapt.sampleHold", "inputs", "trigger") == "none");

    CHECK (polymorphismOf ("osc.analog", "outputs", "out") == "none");
}
