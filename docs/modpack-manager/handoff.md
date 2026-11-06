# Manager client handoff
**Current delivery: read-only status/discovery**, following the clarified status-checking goal. The original writable integration remains unimplemented and deferred; the four-phase write/recovery plan is not complete.
Use [protocol.md](protocol.md), [wire.schema.json](wire.schema.json), and [requests.schema.json](requests.schema.json). Version 1.0 is experimental.

## Implemented
- hello, launcher.info, instances.list, mods.list. Capabilities are authoritative. launcherState is ready; nativeActivity distinguishes idle, queued/auth preparation, an active launch task, deletion and unknown state. gameState stays unknown; idle never authorizes a write.
- Dedicated same-user Windows QLocalServer; bounded length-prefixed JSON, strict envelope/method parameters, paged reads, reconnect/hello, idle timeout and bounded concurrent discovery.
- One coordinator owner per effective data profile. Atomic descriptor publication under integration/v1/endpoint.json; session-aware cleanup. Manager owns starting Launcher.
- Native InstanceList/MinecraftInstance snapshots plus filesystem enumeration of effective roots, including duplicate IDs skipped by InstanceList. Physical directory identities follow real symlinks/junctions; shared game/mod roots and logical collisions are warnings/conflicts.
- Inventory includes filenames, enable state, sizes, times, kinds and native .index provider/project/version identifiers. It uses the existing Packwiz parser on bounded bytes and omits URLs/credentials. It does not parse JAR manifests or verify indexed artifact claims. Observation revisions hash config/.index bytes plus inventory stats, not JAR contents. They never authorize writes.
- No UI redesign, account changes, instance migrations or Manager dependency.

## Availability and client rules
Endpoint publication currently requires Windows and a current-user-owned profile on a fixed local volume without public write access. Ordinary Launcher startup continues if integration is unavailable. Portable/custom profiles work through the same effective paths when they satisfy that access policy. Removable/network profiles and non-Windows IPC are not enabled.
The endpoint appears after ordinary startup/setup, which may need user interaction. A local CMake executable is build/awakelauncher.exe; its child PATH needs the configured Qt bin and dependency DLL directories. Test-only offscreen/synthetic-account settings are not a user startup policy.
Manager must select/resolve the installation and data profile; there is no global installation/profile registry or disk scanner. Read endpoint.json, verify the hello installationId/profileId/sessionId, and ignore stale descriptors. The descriptor is a discovery hint, not authentication; a Windows client should verify the pipe server's OS process/user identity, not trust a descriptor's pid alone. Same-user malicious processes are outside the trust boundary.
Native unloaded collisions are listing-only; UUID/name are unknown beyond their folder label, and mods.list returns NOT_FOUND for those physicalIds. A conflict never permits fallback to a native folder ID.

## Windows client connection
Contract pin for interoperability: `058714ec72ce2db19913f8540b6c4784a9d6ccd2` (protocol 1.0). Retrieve all three linked contract/schema files from that commit when implementing a client; later prose clarifications do not add capabilities.
Parallel agents use distinct private profiles. A shared session requires agreement on contract pin, profile/descriptor and shutdown owner first; never stop another agent's Launcher or edit the other project.
1. Select an installation **and data profile** explicitly. If needed, Manager starts the feature build with that profile; a stable installed binary does not contain this branch automatically.
2. Read `<effective dataRoot>/integration/v1/endpoint.json` with bounded retries during startup/publication, open `\\.\pipe\<serverName>` from a native Windows transport, verify the server's OS identity, then send the documented hello using a 1.0 envelope. A plain browser/Vite frontend cannot open this pipe itself.
3. Check installationId/profileId/sessionId against the selected descriptor; consume the boolean capability map. Query launcher.info, instances.list, then mods.list by physicalId. Scope remembered instances by profileId and physicalId, never folder ID or UUID alone.
4. Serialize requests. Both lists default to 200 entries, maximum 500 per page; aggregate nextOffset pages with the same instanceRevision on continuations. Restart aggregation after STALE_INSTANCE. Read decimal sizes/timestamps without silently losing precision.
5. On disconnect/unavailable Launcher, discard incomplete frames/pages, re-read the selected descriptor, reconnect and hello again. The endpoint/session changes on restart. There are no events; refresh reads explicitly. Connections idle for 30 seconds close; reconnect or use serialized status reads. Never delete a descriptor/lock merely because a connection failed.

## Manager compatibility snapshot
Inspected Manager commit `bafd5d59966155afa64d618d9b1c9606242e5d67`; concurrent uncommitted native-shell work was left untouched and is not covered by this comparison. `src/backend/LauncherClient.ts` rejects every call; `src/main.ts` defaults to MockBackend. No end-to-end Manager session has been validated.

| Manager assumption | Required adaptation |
|---|---|
| Handshake capability array; mods.inventory | Consume the Launcher boolean map; use mods.list. Only hello, launcher.info, instances.list and mods.list are enabled. |
| installations.discover gates refreshInstallations in state/session.ts | Provide explicit profile selection and selected launcher.info; there is no global discovery RPC. Do not advertise a fabricated server capability. |
| InstanceSummary.id/status/manageable/revision | Preserve physical identity and nativeActivity separately. Unknown game state cannot become ready-to-write; allow inspection without implying mutation permission. Observation revisions are not authoritative content hashes. |
| Required launcherVersion/providers, Minecraft/loader details; InstalledMod release type/dependencies/updates | These are unavailable through 1.0. Adapt types/UI to unknown or unavailable; do not manufacture values or treat missing dependencies as verified satisfied. Inventory metadata is an unverified native index claim. |
| getInventory returns one complete array | Aggregate all pages, preserving metadataState and the observation revision. Missing/conflicting metadata remains unknown. |
| Lowercase BackendError codes | Map STALE_INSTANCE to stale-revision; UNSUPPORTED_CAPABILITY to unsupported; BUSY to instance-busy. Preserve protocol/validation/IO failures and retryable; transport unavailability is distinct from unsupported functionality. |

Required client tests: actual native transport/framing and fragmented responses, hello mismatch, selected-profile/session mismatch, empty/305-mod inventories, stale-page restart, collision/unloaded rows, unknown status/metadata, BUSY and unsupported UI gating, unavailable Launcher and reconnect after idle/restart. Keep all planning, provider, export, launch and operation/recovery calls disabled; mock success is not IPC evidence.

## Unavailable; do not enable in Manager
Enrollment, the instance operation gate, provider search/version IPC, instance creation, Evaluate/Prepare/Apply, durable idempotency/operation storage, operation cancellation/events, launch RPC, transaction journals and recovery. Every such request returns UNSUPPORTED_CAPABILITY with no operation created. No no-window QApplication backend has been implemented.
The write policy is approved: explicit enrollment for exclusive use by gate-aware Launcher versions. Enrollment itself remains unavailable. Do not edit live files from Manager.

## Prerequisites for a future writable integration
The gate, quiescence, native serializers, exact baselines, durable staging/backups and recovery-before-startup requirements remain defined in protocol.md. ResourceDownloadTask currently downloads into its supplied live model directory and cannot be used as Prepare storage unchanged. A no-window QApplication lifecycle needs separate proof and a dialog policy.

Relevant source: InstanceList::discoverInstances; INISettingsObject::doSave (immediate); PackProfile::scheduleSave/save_internal (5-second timer); ResourceFolderModel::update/resolveResource (queued reruns and resolver tasks); ResourceFolderLoadTask (renames resources and removes orphan metadata); LocalResourceUpdateTask; LauncherPartLaunch (detachable Java).
This status bridge does not implement the full writable task. Missing gate/quiescence/transaction work is not permission for Manager to bypass those safeguards.

## Review and validation
Compare baseline 6d91edbe0f168c18a334811d7b08e1b6ce89ef16 with feat/modpack-manager-integration. Build AwakeLauncher, AwakeIntegration and AwakeWebBridge using the repository's Windows CMake configuration. Run CTest; GUI/hardware tests require separate execution.
Verified on Windows with MSVC 2022 and Qt 6.11.2:
- Full native build passed. CTest passed 38/38 tests with `ctest --test-dir build --parallel 1 -E '^AwakeWebShell$' --output-on-failure`; QApplication fixtures ran offscreen. Individual live-auth/hardware skips remain skips.
- Integration fixtures covered malformed/fractured frames, negotiation/reconnect, same-profile ownership across processes, public-write ACL rejection, identity collisions, real symlinks/junctions, stale metadata, 305 mods, native index metadata filtering, relative `--dir` discovery and 500 immediate serial reads from a raw Windows pipe subprocess. All instance data was temporary.
- Earlier frontend unit tests passed 50/50. The latest production response capture passed 110 checks against the pinned schemas. The earlier response-schema mismatch is covered by a fixture regression; the readiness review found no actionable issues.

Windows x64 readiness validation passed 18 runtime checks using the actual built Launcher executable, offscreen with synthetic account/instance data and blocked external HTTP. A separate native pipe client verified handshake/version rejection, status, paged discovery, 305 mods over four pages, Modrinth/CurseForge index fields, unsupported operations, stale pages, 1,000 immediate discovery/status requests, reconnect, unavailable Launcher and stable physical IDs across restart. Fixed-local absolute/relative --dir, AWAKELAUNCHER_DATA_DIR, data-location.txt custom/compact, UserData and portable.txt layouts worked; a public-write profile was refused without stopping Launcher. These are test settings, not a supported no-window backend. Default user AppData and actual removable/network profiles were not opened.

One early standalone run observed ERROR_PIPE_NOT_CONNECTED; repeated diagnostic sessions and a separate-process serial-read test did not establish its cause. No server fix is claimed. A transient descriptor-read sharing failure also occurred during publication; clients must use bounded retries and reconnect. Manager end-to-end readiness remains unproven until its native adapter passes the client tests above.

Not executed: the visible AwakeWebShell suite, real Minecraft launches, live sign-in, hardware selection, cross-user/remote IPC attempts, or writable-operation/crash-recovery fault tests. No gate, Apply or recovery capability is implemented, so these results do not establish safe external mutation. Test tools/logs stay outside the committed product; no validator dependency was added to Launcher.
