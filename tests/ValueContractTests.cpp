#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include "bazalt/engine/graph/ValueTypes.h"
#include "bazalt/engine/nodes/CurvePlayerNode.h"

using namespace bazalt::engine;

// M14: the value-contract migration (PortDescriptor.h/ParameterDescriptor)
// added ValueKind/Quantity/Curve/Polarity/enumOptions/step/softMin/softMax/
// isStructural/PortGroup as new, defaulted fields — this suite is the
// regression net the migration's own exit criteria promised: every existing
// numeric literal a factory or node already returned must be bit-identical,
// and the new fields must default to "no real tag set" everywhere they
// aren't explicitly assigned, matching what a not-yet-migrated port/
// parameter looked like before this file existed.

TEST_CASE ("ValueTypes factories keep their pre-M14 numeric shape exactly",
           "[engine][ValueTypes][M14]")
{
    SECTION ("frequencyParameter")
    {
        const auto p = ValueTypes::frequencyParameter ("x.frequency", "Frequency", 440.0f);
        CHECK (p.minValue == 20.0f);
        CHECK (p.maxValue == 20000.0f);
        CHECK (p.defaultValue == 440.0f);
        CHECK (p.skew == 0.3f);
        CHECK (p.unit == "Hz");
        CHECK (p.quantity == Quantity::Frequency);
        CHECK (p.curve == Curve::Logarithmic);
        CHECK (p.kind == ValueKind::Float); // unchanged default
    }

    SECTION ("frequencyPort")
    {
        const auto p = ValueTypes::frequencyPort ("x.freq", "Freq");
        CHECK (p.minValue == 20.0f);
        CHECK (p.maxValue == 20000.0f);
        CHECK (p.defaultValue == 440.0f);
        CHECK (p.isLogScale);
        CHECK (p.hasFallbackWhenUnconnected);
        CHECK (p.quantity == Quantity::Frequency);
        CHECK_FALSE (p.group.has_value()); // ordinary port, not part of a growable group
    }

    SECTION ("timeSecondsParameter")
    {
        const auto p = ValueTypes::timeSecondsParameter ("x.attack", "Attack", 0.01f);
        CHECK (p.minValue == 0.0f);
        CHECK (p.maxValue == 10.0f);
        CHECK (p.defaultValue == 0.01f);
        CHECK (p.skew == 0.5f);
        CHECK (p.unit == "s");
        CHECK (p.quantity == Quantity::Time);
    }

    SECTION ("timeSamplesPort")
    {
        const auto p = ValueTypes::timeSamplesPort ("x.samples", "By", 4096, 200.0f);
        CHECK (p.minValue == 1.0f);
        CHECK (p.maxValue == 4096.0f);
        CHECK (p.defaultValue == 200.0f);
        CHECK (p.isInteger);
        CHECK (p.unit == "samples");
        CHECK (p.kind == ValueKind::Int);
        CHECK (p.quantity == Quantity::Time);
        CHECK (p.step == 1.0f);
    }
}

TEST_CASE ("Unmigrated ports/parameters default every new value-contract field to its no-op value",
           "[engine][PortDescriptor][M14]")
{
    // A plain, pre-M14-style aggregate init — exactly what every node
    // header still writes for a bespoke value ValueTypes.h doesn't cover
    // (e.g. filter.svf's resonance, filter.onepole's coefficient).
    const PortDescriptor port { .id = "in", .type = SignalType::Signal };
    CHECK (port.kind == ValueKind::Float);
    CHECK (port.quantity == Quantity::Dimensionless);
    CHECK (port.curve == Curve::Linear);
    CHECK (port.polarity == Polarity::Unipolar);
    CHECK (port.enumOptions.empty());
    CHECK (port.step == 0.0f);
    CHECK_FALSE (port.softMin.has_value());
    CHECK_FALSE (port.softMax.has_value());
    CHECK_FALSE (port.group.has_value());

    const ParameterDescriptor parameter { "x.resonance", 0.01f, 10.0f, 0.707f, 0.5f, "" };
    CHECK (parameter.kind == ValueKind::Float);
    CHECK (parameter.quantity == Quantity::Dimensionless);
    CHECK_FALSE (parameter.isStructural);
}

TEST_CASE ("source.oscillator.division is a real, correctly-ordered enum",
           "[engine][CurvePlayerNode][M14]")
{
    const nodes::CurveOscillatorNode osc;
    const auto parameters = osc.getParameters();
    const auto it = std::find_if (parameters.begin(), parameters.end(),
                                   [] (const ParameterDescriptor& p) { return p.id == "source.oscillator.division"; });
    REQUIRE (it != parameters.end());

    CHECK (it->kind == ValueKind::Enum);
    REQUIRE (it->enumOptions.size() == 9);
    // Order matches the beats table in CurvePlayerNode::setParameter.
    CHECK (it->enumOptions[0].id == "8bars");
    CHECK (it->enumOptions[3].id == "1bar");
    CHECK (it->enumOptions[5].id == "1/4");
    CHECK (it->enumOptions[8].id == "1/32");
    CHECK (it->minValue == 0.0f);
    CHECK (it->maxValue == 8.0f);
    CHECK (it->defaultValue == 4.0f);
    CHECK (it->isInteger);
}
