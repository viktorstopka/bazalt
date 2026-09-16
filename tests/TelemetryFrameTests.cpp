#include <catch2/catch_test_macros.hpp>
#include "bazalt/engine/telemetry/TelemetryFrame.h"

using namespace bazalt::engine;

TEST_CASE ("A telemetry frame round-trips its header and payload exactly", "[engine][telemetry]")
{
    TelemetryFrameHeader header;
    header.tapId = 3;
    header.frameType = TelemetryFrameType::Spectrum;
    header.sampleRate = 48000.0f;
    header.sequenceNumber = 12345;

    float payload[5] = { 1.0f, -2.5f, 3.25f, 0.0f, 100.0f };

    std::vector<std::byte> bytes;
    serializeTelemetryFrame (header, payload, 5, bytes);

    TelemetryFrameHeader parsedHeader;
    const float* parsedPayload = nullptr;
    REQUIRE (parseTelemetryFrame (bytes.data(), bytes.size(), parsedHeader, parsedPayload));

    CHECK (parsedHeader.tapId == 3);
    CHECK (parsedHeader.frameType == TelemetryFrameType::Spectrum);
    CHECK (parsedHeader.sampleRate == 48000.0f);
    CHECK (parsedHeader.sequenceNumber == 12345);
    CHECK (parsedHeader.payloadNumFloats == 5);

    REQUIRE (parsedPayload != nullptr);
    for (int i = 0; i < 5; ++i)
        CHECK (parsedPayload[i] == payload[i]);
}

TEST_CASE ("Parsing rejects a buffer too short for its declared payload", "[engine][telemetry]")
{
    TelemetryFrameHeader header;
    float payload[10] = {};

    std::vector<std::byte> bytes;
    serializeTelemetryFrame (header, payload, 10, bytes);

    bytes.resize (bytes.size() - 4); // truncate — one float short

    TelemetryFrameHeader parsedHeader;
    const float* parsedPayload = nullptr;
    CHECK_FALSE (parseTelemetryFrame (bytes.data(), bytes.size(), parsedHeader, parsedPayload));
}

TEST_CASE ("Parsing rejects a buffer shorter than the header itself", "[engine][telemetry]")
{
    std::vector<std::byte> tooShort (4, std::byte { 0 });

    TelemetryFrameHeader parsedHeader;
    const float* parsedPayload = nullptr;
    CHECK_FALSE (parseTelemetryFrame (tooShort.data(), tooShort.size(), parsedHeader, parsedPayload));
}
