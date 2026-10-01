#pragma once

#include <cstdint>

namespace bazalt::engine::nodes
{
    /** Boost's classic 64-bit hash_combine — not cryptographic, just a
        cheap, well-known, deterministic mix so nearby seeds/ordinals don't
        produce visibly-correlated streams. Same (seed, ordinal) pair always
        produces the same int64, on any platform, any run — that determinism
        is the entire point.

        Shared by every instance-allocating node type (`InstanceVoiceNode`
        and its Domain Extensions siblings — `InstanceSwarmPopulationNode`/
        `InstanceSwarmTransientNode`/`InstanceTriggerNode`) that needs a
        per-instance-lifetime-stable random value derived from (patch seed,
        spawn/slot ordinal) — `archive_docs/DOMAINS.md` §4's own determinism
        requirement: "the same patch, the same MIDI, the same seed produce
        bit-identical output... required for the offline render CLI to be a
        useful regression tool." First built for `InstanceVoiceNode` alone
        (`09-28-InstanceAllocator.2`); factored out here once a second node
        type needed the identical mix, rather than duplicating it.
    */
    inline int64_t combineInstanceSeed (int seed, int ordinal) noexcept
    {
        uint64_t h = (uint64_t) (uint32_t) seed;
        h ^= (uint64_t) (uint32_t) ordinal + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return (int64_t) h;
    }
}
