#pragma once

#include <cstdint>

namespace bazalt::engine::nodes
{
    /** A cheap, well-known, deterministic mix (SplitMix64), not cryptographic, so nearby seeds/ordinals don't
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
        // SplitMix64's finaliser over (seed, ordinal): every input bit
        // changes about half the output bits. The old Boost hash_combine
        // only changed the low bits between neighbouring ordinals, and
        // juce::Random's FIRST draw barely depends on those — so every
        // member's Random 1 came out nearly equal (wiki/plans/Release.md R0.4),
        // and Material's per-mode jitter nearly equal across modes.
        uint64_t h = ((uint64_t) (uint32_t) seed << 32) ^ (uint64_t) (uint32_t) ordinal;
        h += 0x9e3779b97f4a7c15ULL;
        h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
        h = (h ^ (h >> 27)) * 0x94d049bb133111ebULL;
        h ^= h >> 31;
        return (int64_t) h;
    }
}
