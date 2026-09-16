#pragma once

namespace bazalt::engine
{
    /** Semantic version of the engine library itself, independent of the
        plugin/host-facing version. Bumped whenever the engine's public
        surface (NodeGraph, ExecutionPlan, Node interface) changes in a way
        that matters to callers such as tools/render-cli.
    */
    struct Version
    {
        int major;
        int minor;
        int patch;
    };

    Version getVersion() noexcept;
    const char* getVersionString() noexcept;
}
