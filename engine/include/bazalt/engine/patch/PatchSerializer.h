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

    /** Parses and runs the vN -> vN+1 migration chain up to
        PatchDocument::currentSchemaVersion (ARCHITECTURE.md §4.4). Fails
        rather than guessing if the file is newer than this build supports,
        or older than any registered migration can reach.
    */
    PatchParseResult parsePatchFromJson (const juce::String& json);
}
