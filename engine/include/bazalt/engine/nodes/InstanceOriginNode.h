#pragma once

namespace bazalt::engine::nodes
{
    /** wiki/NODES.Status.md's "Domain Extensions" batch — shared by every
        node type that opens an instanced (Poly) region: `InstanceVoiceNode`
        ("instance.allocate.voice") and its Swarm-transient/Trigger siblings
        (`instance.allocate.swarmTransient`/`instance.allocate.trigger`).
        `instance.allocate.swarmPopulation` deliberately does NOT implement
        this — per its own design ("fixed count, always live... no spawn
        logic", `archive_docs/DOMAINS.md` §3), it never spawns or releases
        anything after `prepare()`, so there is nothing for this interface's
        spawn/release dispatch to do for it at all.

        `PluginProcessor::findAllocatorNode()`/`renderOriginVoiceRange()`'s
        internal-trigger-relay mechanism (an Event firing an internal spawn/
        release, detected via `consumeSpawnEventsThisBlock()` and relayed
        across this origin's own voice-pool slots — the exact mechanism that
        already lets a purely-internal clock->seq->note.assemble chain drive
        Voice with no MIDI involved at all, DomainRedesign.md Batch 1b)
        operates against this interface, not a concrete node class, so
        adding a new event-driven origin TYPE never requires touching that
        dispatch code again — only a new node class that implements this
        interface.

        `getPitch()`/`getVelocity()`/the payload arguments to
        `spawnInstance()` exist ONLY so the SAME relay-dispatch code can
        call either a Voice origin (where they're real, meaningful values)
        or a Swarm-transient/Trigger origin (where they're ignored — no such
        concept exists for those types) without branching on concrete type.
        Real MIDI dispatch (`PluginProcessor::handleMidiEvent`/
        `triggerVoiceNoteViaAllocator`) stays scoped to the concrete
        `InstanceVoiceNode*` and real pitch/velocity, unchanged by this
        interface's existence — MIDI only ever drives Voice, by design
        (`DOMAINS.md` §3's own table: Swarm/Trigger spawn from an `Event`
        stream or a fixed count, never from MIDI).
    */
    class InstanceOriginNode
    {
    public:
        virtual ~InstanceOriginNode() = default;

        virtual bool getGate() const noexcept = 0;

        /** See `InstanceVoiceNode::consumeSpawnEventsThisBlock()`'s own doc
            comment for the full mechanism this backs — one counter for both
            spawn and release, incremented on either, consumed (reset to 0)
            by whoever's watching this block.
        */
        virtual int consumeSpawnEventsThisBlock() noexcept = 0;

        /** Defaults are the "no real concept of this" values (the
            `osc.analog`/`InstanceVoiceNode` pitch anchor, full-strength
            velocity) — Swarm-transient/Trigger never override these,
            Voice's own override just forwards to its real stored values.
        */
        virtual float getPitch() const noexcept { return 60.0f; }
        virtual float getVelocity() const noexcept { return 1.0f; }

        /** Voice: forwards to `noteOn(pitch, velocity)`. Swarm-transient/
            Trigger: spawn/retrigger with the arguments ignored entirely —
            they exist only so one relay-dispatch call site can target
            either kind of origin uniformly.
        */
        virtual void spawnInstance (float pitch, float velocity) noexcept = 0;
        virtual void releaseInstance() noexcept = 0;

        /** `GraphEditController`'s compile-time counterpart to the render-
            time dispatch above: captured once per recompile off slot 0's
            own compiled copy and enforced via `VoiceManager::
            setMaxActiveVoices` (wiki/plans/DomainRedesign.md Batch 4's own
            mechanism, now generalized past `InstanceVoiceNode*`). Defaults
            to 1 — Trigger (Batch 4) never overrides this at all, matching
            its own "maxInstances is implicitly 1, not an exposed parameter"
            design; Voice and Swarm-transient both override with their real
            stored parameter value.
        */
        virtual int getMaxInstances() const noexcept { return 1; }
    };
}
