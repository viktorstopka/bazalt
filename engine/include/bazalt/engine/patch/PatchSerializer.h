#pragma once

#include "bazalt/engine/patch/PatchDocument.h"

namespace bazalt::engine
{
    juce::String serializePatchToJson (const PatchDocument& doc, bool prettyPrint = true);

    struct PatchParseResult
    {
        bool success = false;
        PatchDocument document;
        juce::String errorMessage;
    };

    /** Parses a patch of PatchDocument::currentSchemaVersion. Any other
        version fails with a plain message — no migrations until the ID freeze
        (wiki/plans/Release.md R0.6).
    */
    PatchParseResult parsePatchFromJson (const juce::String& json);
}
