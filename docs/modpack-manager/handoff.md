# Manager client handoff
**Current external delivery: read-only status/discovery.** Mandatory standalone Manager support requires an approved architecture revision before further migration. Existing transaction/recovery foundations are retained; write acceptance criteria are incomplete and no write capability is enabled.
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
Current contract/schema pin: `d3e6731a8a3d9a668ad6509eb795eb73030c0823` (implemented protocol 1.0). Retrieve protocol.md, wire.schema.json and requests.schema.json together from that commit. Existing read-only clients pinned to `058714ec72ce2db19913f8540b6c4784a9d6ccd2` remain compatible. The newer pin adds only reserved rolling_back/terminal consistency and ownership clarifications; no operation RPC or new capability is enabled. Re-pin again when concrete operation schemas and negotiated extensions are implemented.
Parallel agents use distinct private profiles. A shared session requires agreement on contract pin, profile/descriptor and shutdown owner first; never stop another agent's Launcher or edit the other project.
1. Select an installation **and data profile** explicitly. If needed, Manager starts the feature build with that profile; a stable installed binary does not contain this branch automatically.
2. Read `<effective dataRoot>/integration/v1/endpoint.json` with bounded retries during startup/publication, open `\\.\pipe\<serverName>` from a native Windows transport, verify the server's OS identity, then send the documented hello using a 1.0 envelope. A plain browser/Vite frontend cannot open this pipe itself.
3. Check installationId/profileId/sessionId against the selected descriptor; consume the boolean capability map. Query launcher.info, instances.list, then mods.list by physicalId. Scope remembered instances by profileId and physicalId, never folder ID or UUID alone.
4. Serialize requests. Both lists default to 200 entries, maximum 500 per page; aggregate nextOffset pages with the same instanceRevision on continuations. Restart aggregation after STALE_INSTANCE. Read decimal sizes/timestamps without silently losing precision.
5. On disconnect/unavailable Launcher, discard incomplete frames/pages, re-read the selected descriptor, reconnect and hello again. The endpoint/session changes on restart. There are no events; refresh reads explicitly. Connections idle for 30 seconds close; reconnect or use serialized status reads. Never delete a descriptor/lock merely because a connection failed.

## Manager compatibility snapshot
Read-only inspection at Manager commit `84e4228ae6ff8a31f85b6e350b36d589db093433` found the native desktop client in `src/backend/launcher/LauncherClient.ts`, serialized transport, paging and capability-gated draft operations. The earlier `bafd5d5` stub comparison below is historical; it does not describe the current desktop client. Concurrent untracked Manager work was untouched. The user reports a successful real Tauri/pipe connection with 300+ entries; this Launcher session did not independently rerun Manager end-to-end tests.

| Historical Manager assumption | Contract requirement (verify current client before adapting) |
|---|---|
| Handshake capability array; mods.inventory | Consume the Launcher boolean map; use mods.list. Only hello, launcher.info, instances.list and mods.list are enabled. |
| installations.discover gates refreshInstallations in state/session.ts | Provide explicit profile selection and selected launcher.info; there is no global discovery RPC. Do not advertise a fabricated server capability. |
| InstanceSummary.id/status/manageable/revision | Preserve physical identity and nativeActivity separately. Unknown game state cannot become ready-to-write; allow inspection without implying mutation permission. Observation revisions are not authoritative content hashes. |
| Required launcherVersion/providers, Minecraft/loader details; InstalledMod release type/dependencies/updates | These are unavailable through 1.0. Adapt types/UI to unknown or unavailable; do not manufacture values or treat missing dependencies as verified satisfied. Inventory metadata is an unverified native index claim. |
| getInventory returns one complete array | Aggregate all pages, preserving metadataState and the observation revision. Missing/conflicting metadata remains unknown. |
| Lowercase BackendError codes | Map STALE_INSTANCE to stale-revision; UNSUPPORTED_CAPABILITY to unsupported; BUSY to instance-busy. Preserve protocol/validation/IO failures and retryable; transport unavailability is distinct from unsupported functionality. |

Required client tests: actual native transport/framing and fragmented responses, hello mismatch, selected-profile/session mismatch, empty/305-mod inventories, stale-page restart, collision/unloaded rows, unknown status/metadata, BUSY and unsupported UI gating, unavailable Launcher and reconnect after idle/restart. Keep unsupported Launcher IPC calls disabled; mock success is not IPC evidence. This does not redefine the mandatory standalone Manager scope.

## Unavailable; do not enable in Manager
Internal Windows foundations now include durable idempotent operation records, private recovery storage, strong volume/file identities, a cross-process physical lease and a recoverable multi-file executor. Apply requires exact hashes/revision/digest and stops at verifying until native refresh permits explicit completion. Restart rolls back before durable commit intent, or verifies and completes after it. Unknown bytes and corrupt records retain recovery evidence. These foundations are not advertised IPC capabilities; native writers, game ownership, startup recovery and supported filesystem metadata handling must be integrated before external writes.
Enrollment, the instance operation gate, provider search/version IPC, instance creation, Evaluate/Prepare/Apply, durable idempotency/operation storage, operation cancellation/events, launch RPC, transaction journals and recovery. Every such request returns UNSUPPORTED_CAPABILITY with no operation created. No no-window QApplication backend has been implemented.
The write policy is approved: explicit enrollment for exclusive use by gate-aware Launcher versions. Enrollment itself remains unavailable. Manager must not directly edit Launcher-managed live files. Standalone Manager must create, install, maintain, update and export real modpacks without Launcher; current IPC does not satisfy that requirement. Shared-core packaging and ownership transfer are proposals awaiting user approval, not a newly supported mode.

## Prerequisites for a future writable integration
The gate, quiescence, native serializers, exact baselines, durable staging/backups and recovery-before-startup requirements remain defined in protocol.md. ResourceDownloadTask currently downloads into its supplied live model directory and cannot be used as Prepare storage unchanged. A no-window QApplication lifecycle needs separate proof and a dialog policy.

Relevant source: InstanceList::discoverInstances; INISettingsObject::doSave (immediate); PackProfile::scheduleSave/save_internal (5-second timer); ResourceFolderModel::update/resolveResource (queued reruns and resolver tasks); ResourceFolderLoadTask (renames resources and removes orphan metadata); LocalResourceUpdateTask; LauncherPartLaunch (detachable Java).
This status bridge does not implement the full writable task. Missing gate/quiescence/transaction work is not permission for Manager to bypass those safeguards.

## Claude requests and remaining sequence
Reviewed `docs/LAUNCHER-REQUESTS.md` read-only. No new RPC is enabled by this revision.

| Request | Contract status / prerequisite |
|---|---|
| R0 | Reserved operation schema now includes nonterminal `rolling_back` and enforces terminal flags. Existing 1.0 read payloads are unchanged. Re-pin before exposing operations; internal store records need a wire adapter, not verbatim serialization. |
| R1–R2 | Detailed Minecraft/loader fields, paged catalogs, exact loader create shape, configured root IDs and created physicalId remain undefined/unavailable. Negotiate a new minor for extra read fields. |
| R3 | Search/version/dependency schemas need permission, distribution and download availability fields. Plain read RPCs are preferable to durable search operations; this revises the disabled draft and needs a client re-pin. Credentials stay native. |
| R4 | Prepare/evaluate must define exact changes, required/optional dependencies, conflicts, affected-file pagination, hashes, disk budget, expiry and reviewed digest. Update representation is unresolved; no guessed shape. |
| R5–R6 | JAR manifest inspection and batched update lookup are absent. Native index claims remain unverified. Manager-only pins/exclusions would conflict with canonical instance policies: Manager chooses intent, the owning backend must persist/reconcile it with native locks without erasing pins. Exact policy schema awaits design approval. |
| R7–R8 | Durable list/get/cancel/progress, recovery wrappers/actions and timestamps need public result schemas. Internal states/storage do not imply public capabilities. |
| R9–R10 | Bounded content categories, upstream baselines and three-way pack conflict handling remain unavailable. Never accept arbitrary caller paths or delete unknown files silently. |
| R11 | Export requires a consistent snapshot, hashes, provider redistribution rules and explicit output selection; it must work in standalone Manager too. No export capability exists yet. |
| R12 | `--dir <dataRoot>` is supported and fixture-tested. Endpoint-not-ready and unsupported-build states must stay distinct. No production no-window mode or minimized/tray startup contract is implemented. |

After architecture approval, implement in this order:
1. Define common service context and per-root authority/version compatibility; preserve 1.0 reads. Fix the executor's expected post-plan inventory revision before exposing Apply: sampling a fresh revision after writes could accept an unrelated concurrent edit.
2. Wire the fixed cross-process registry/enrollment gate into native launch/auth preparation, mod downloads/updates/toggles, rename/delete, component edits, metadata writes and resource scans. Quiesce watchers, tasks, queued callbacks and retained save timers; flush before exclusive acquisition, reload with saves suspended, then verify. Whole-root replacement requires explicit identity transfer or a safe unsupported result.
3. Run recovery before constructing native models that scan/save. Missing/corrupt game or ownership evidence blocks writes and launch; never infer safety from disconnect. Integrate keeper ownership into real launch before claiming detached-game protection.
4. Implement catalogs, provider reads/dependencies, policy reconciliation and native instance creation with exact version/loader validation, controlled staging and failure cleanup. Use effective configured roots, not caller filesystem paths.
5. Implement evaluate/prepare and Apply using native serializers, exact file baselines, verified staged artifacts and durable results. Test real provider files, unknown files, stale metadata, pending saves, 305-mod batches and interrupted rollback/recovery before enabling writes.
6. Expose kind-specific operation get/cancel/list and reconnect first, then each tested write capability. Events cannot replace durable status. Wire launch through existing authentication/Java/GPU flow with explicit interaction-required handoff.
7. Validate independently bundled standalone runtime create/install/update/export without Launcher present, then shared-instance ownership transfer and Manager interoperability against a committed contract. Use separate private profiles; agree on pin, profile and shutdown owner before a shared runtime session. Keep Manager files untouched.

## Review and validation
Compare baseline 6d91edbe0f168c18a334811d7b08e1b6ce89ef16 with feat/modpack-manager-integration. Build AwakeLauncher, AwakeIntegration and AwakeWebBridge using the repository's Windows CMake configuration. Run CTest; GUI/hardware tests require separate execution.
Verified on Windows with MSVC 2022 and Qt 6.11.2:
- Full native build passed. CTest passed 40/40 tests with `ctest --test-dir build --parallel 1 -E '^AwakeWebShell$' --output-on-failure`; QApplication fixtures ran offscreen. Individual live-auth/hardware skips remain skips.
- Integration fixtures covered malformed/fractured frames, negotiation/reconnect, same-profile ownership across processes, public-write ACL rejection, identity collisions, real symlinks/junctions, stale metadata, 305 mods, native index metadata filtering, relative `--dir` discovery and 500 immediate serial reads from a raw Windows pipe subprocess. All instance data was temporary.
- Earlier frontend unit tests passed 50/50. The latest production response capture passed 110 checks against the pinned schemas. The earlier response-schema mismatch is covered by a fixture regression; the readiness review found no actionable issues.

Windows x64 readiness validation passed 18 runtime checks using the actual built Launcher executable, offscreen with synthetic account/instance data and blocked external HTTP. A separate native pipe client verified handshake/version rejection, status, paged discovery, 305 mods over four pages, Modrinth/CurseForge index fields, unsupported operations, stale pages, 1,000 immediate discovery/status requests, reconnect, unavailable Launcher and stable physical IDs across restart. Fixed-local absolute/relative --dir, AWAKELAUNCHER_DATA_DIR, data-location.txt custom/compact, UserData and portable.txt layouts worked; a public-write profile was refused without stopping Launcher. These are test settings, not a supported no-window backend. Default user AppData and actual removable/network profiles were not opened.

One early standalone run observed ERROR_PIPE_NOT_CONNECTED; repeated diagnostic sessions and a separate-process serial-read test did not establish its cause. No server fix is claimed. A transient descriptor-read sharing failure also occurred during publication; clients must use bounded retries and reconnect. Manager end-to-end readiness remains unproven until its native adapter passes the client tests above.

Internal transaction fixtures additionally cover 305 changed files, exact final hashes, explicit removal/disable moves, stale plans and staging, process termination during replacement and rollback, commit-intent recovery, injected disk-full failure, locked files, unknown edits and corrupt journals. Storage tests reject public-read recovery records; lease tests use independent processes and verify root pinning. All data is isolated. These tests do not establish native writer coordination or safe external mutation.

The internal executor rejects alternate data streams, special file attributes and permissions/ownership differing from a newly inherited file; it preserves those files rather than flattening their metadata. It pins directory ancestors and existing mod subdirectories through verification. Instance/component/policy metadata can be replaced with staged serializer output, never removed. If a terminal operation retains a recovery marker, cleanup re-verifies its files and revision and preserves the marker after later edits. Native save flushing, model refresh and enrollment validation remain prerequisites for any public Apply capability.

Game ownership foundations use creation-time Windows job assignment and a per-game keeper mode in the same executable, before QApplication/web initialization. The keeper retains a reduced-access query handle, survives Launcher exit, waits for the complete process group, then flushes and publishes a completion certificate. A failed certificate or missing ownership evidence remains unknown and blocks leases/transactions. Fixtures terminate both parent and wrapper while a descendant survives, verify cross-process exclusion, then verify release after exit. This is internal infrastructure: native launch hooks and detection of untracked legacy games remain required. It does not implement the Manager backend lifecycle.

Windows native Tasks can retain an explicitly scoped instance lease through execution, queued callbacks and child tasks, release it after terminal callbacks, and reacquire on retry. QApplication event dispatch clears unrelated ownership during nested event loops; affiliated Task events restore their own scope. These propagation foundations do not acquire permits at native mutation entry points. Plain QObject callbacks, pending native serializers, startup recovery and launch coordination still require integration; do not enable writes from these foundations alone.

Shared ResourceAPI callbacks now report unsupported requests and malformed version/dependency JSON, propagate dependency cancellation, and handle failures before any HTTP action starts. Loopback and child-process fixtures cover these paths; native provider IPC and live credentials/download permissions are still unavailable or unverified.

Native network completion now uses Task lifecycle notifications after sink cleanup. Cancellation closes partial files immediately, cancels queued children and releases their leases; retained failed download history no longer retains an active lease. Retry, cache-hit and request-body failure fixtures cover cleanup ordering. Full Windows build and 40/40 non-visible CTest suites passed; focused Task results: 22 passed, provider: 23 passed/5 skipped, authentication: 20 passed/1 skipped. Skipped live-service tests remain unverified.

Not executed: the visible AwakeWebShell suite, real Minecraft launches, live sign-in, hardware selection, cross-user/remote IPC attempts, or Manager writable workflows. Public gate, Apply and recovery capabilities remain unavailable. Test tools/logs stay outside the committed product; no validator dependency was added to Launcher.

## Launcher-only performance comparison
Measured native source at `69ebde4f5e8730bd99b641166bd70ffeb9bea1c3` against detached main `6d91edbe0f168c18a334811d7b08e1b6ce89ef16`. Both Windows x64 builds used MSVC 2022, Qt 6.11.2, Release, LTO off and web assets compiled. Runtime exercised offscreen widgets on Windows build 26300 / i5-13400F (16 logical processors), without Manager or IPC clients. External HTTP used a refused loopback proxy.

Fresh private synthetic profiles, alternating build order: one discarded warm-up and five measured runs per build/workload (24 total). Startup ends at the same native main-window log milestone; idle sampling follows five seconds of settling and lasts ten seconds. These are synthetic files, not real mods.

| Mods | Main startup median (range), ms | Feature startup median (range), ms | Main / feature private memory median, MiB |
|---|---|---|---|
| 0 | 796 (727–866) | 782 (666–979) | 38.879 / 38.988 |
| 305 | 938 (816–1728) | 949 (896–3531) | 38.980 / 38.945 |

Idle CPU medians were zero; maximum measured empty-profile samples were 1.562 CPU ms/second in each build. All sampled idle read/write byte deltas were zero. Feature startup created only integration/v1/endpoint.json and coordinator.lock in integration storage; no operation/staging activity was observed. All fixture mod counts and hashes were preserved.

Memory/idle overhead was small in this fixture. Startup variance, particularly the 3.53-second feature outlier, remains unexplained; these samples cannot establish absence of startup regressions. Visible WebEngine, real authentication, Java/GPU behavior and Minecraft launch performance were not measured. Recheck after shared-core/native gate integration and in representative interactive use. Local reproducible harness/raw results are retained under ignored .superpowers; no performance tooling dependency ships with Launcher.
