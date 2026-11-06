# Manager client handoff
Implement against [protocol.md](protocol.md) and [wire.schema.json](wire.schema.json). Protocol 1.0 is experimental.
- Supported target: read-only integration while Launcher runs. Manager decides if/when to start Launcher.
- No no-window backend, provider IPC, instance creation, plan/apply, durable operations/events, launch RPC or recovery RPC is enabled yet. Capability false must disable client actions.
- Native candidates: InstanceList/MinecraftInstance for discovery, ResourceAPI and modplatform/modrinth/ModrinthAPI + modplatform/flame/FlameAPI for providers, PackProfile and Packwiz serializers for metadata. Keep secrets native.
- Safety prerequisites: complete gate coverage, crash-surviving Java verification, quiesce/flush/refresh protocol and proven durable recovery. Independent older Launcher copies cannot be assumed to honor new locks.
- Write policy approved: explicit enrollment and gate-aware Launcher versions. No enrollment is enabled yet.
- Decisions still needed before writes: supported crash-durability filesystems, operation retention/expiry, no-window interaction policy, enforcement of enrollment against unmodified Launcher binaries sharing roots.
- Current native hazards: INISettingsObject saves immediately; PackProfile has deferred saves; ResourceFolderModel owns asynchronous parse/update tasks; resource metadata can be written by tasks; InstanceList skips duplicate folder IDs. Generic file replacement would bypass those lifecycles.
- Testing evidence will be updated after execution. Do not interpret draft write schemas as implemented.
