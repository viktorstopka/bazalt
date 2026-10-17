// What arithmetic on quantities means (QuantityAlgebra.h): Hz × a number is Hz,
// so a frequency computed through Multiply / Add reaches a Hz port without an
// identity Map in front of it.
#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/graph/CanConnect.h"
#include "bazalt/engine/graph/QuantityAlgebra.h"
#include "bazalt/engine/nodes/AddNode.h"
#include "bazalt/engine/nodes/RemapNode.h"
#include "bazalt/engine/nodes/MultiplyNode.h"

using namespace bazalt::engine;
using Op = quantityAlgebra::Operation;

namespace
{
    Quantity of (Op op, std::initializer_list<Quantity> qs)
    {
        std::array<Quantity, 4> quantities {};
        std::array<bool, 4> present {};
        size_t i = 0;
        for (auto q : qs) { quantities[i] = q; present[i] = true; ++i; }
        return quantityAlgebra::combine (op, quantities, present);
    }

    PortDescriptor out (Quantity q) { return PortDescriptor { .id = "out", .type = SignalType::Signal, .quantity = q }; }
}

TEST_CASE ("A unit survives arithmetic with plain numbers", "[engine][quantity]")
{
    CHECK (of (Op::Product, { Quantity::Frequency, Quantity::Dimensionless }) == Quantity::Frequency);
    CHECK (of (Op::Product, { Quantity::Frequency, Quantity::Audio }) == Quantity::Frequency); // an LFO times a Hz depth
    CHECK (of (Op::Product, { Quantity::Audio, Quantity::Unipolar }) == Quantity::Audio);       // a VCA
    CHECK (of (Op::Product, { Quantity::Audio, Quantity::Gain }) == Quantity::Audio);           // a gain is a number in a product
    CHECK (of (Op::Product, { Quantity::Frequency, Quantity::Frequency }) == Quantity::Dimensionless);
    CHECK (of (Op::Product, { Quantity::Frequency, Quantity::Time }) == Quantity::Dimensionless);
    CHECK (of (Op::Sum, { Quantity::Pitch, Quantity::Bipolar }) == Quantity::Pitch);              // a pitch + a wobble
    CHECK (of (Op::Sum, { Quantity::Pitch, Quantity::Pitch }) == Quantity::Pitch);
    CHECK (of (Op::Sum, { Quantity::Pitch, Quantity::Frequency }) == Quantity::Dimensionless);
    CHECK (of (Op::Sum, { Quantity::Audio, Quantity::Audio }) == Quantity::Audio);
    CHECK (of (Op::Sum, { Quantity::Unipolar, Quantity::Unipolar }) == Quantity::Unipolar);
}

TEST_CASE ("A Map with a unit, times an LFO, reaches a Hz port directly", "[engine][quantity]")
{
    nodes::RemapNode map;
    map.setParameter ("math.remap.quantity", (float) Quantity::Frequency);
    REQUIRE (map.getOutputPorts()[0].quantity == Quantity::Frequency);

    nodes::MultiplyNode product;
    product.resolveIncomingPort ("in.0", map.getOutputPorts()[0]);
    product.resolveIncomingPort ("in.1", out (Quantity::Audio));
    const auto result = product.getOutputPorts()[0];
    CHECK (result.quantity == Quantity::Frequency);

    const auto hzPort = PortDescriptor { .id = "f", .type = SignalType::Signal, .minValue = 0.01f, .maxValue = 20000.0f, .quantity = Quantity::Frequency };
    CHECK (canConnect (result, hzPort).outcome == ConnectionOutcome::Ok);
}
