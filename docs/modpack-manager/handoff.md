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
Manager must select/resolve the installation and data profile; there is no global installation/profile registry or disk scanner. Read endpoint.json, verify the hello installationId/profileId/sessionId, and ignore stale descriptors. The descriptor is a discovery hint, not authentication; a Windows client should verify the pipe server's OS process/user identity, not trust a descriptor's pid alone. Same-user malicious processes are outside the trust boundary.
Native unloaded collisions are listing-only; UUID/name are unknown beyond their folder label, and mods.list returns NOT_FOUND for those physicalIds. A conflict never permits fallback to a native folder ID.
Both lists default to 200 entries, maximum 500 per page. Follow nextOffset and echo instanceRevision on every continuation. On STALE_INSTANCE restart the list. Retry reads only after hello on reconnect.
Mock hello/request/disconnect, empty libraries, aliases/collisions, 300+ mods, revision changes, large/error frames, BUSY, unavailable capabilities and stale sessions. No accepted side effects or durable operations are implemented.

## Unavailable; do not enable in Manager
Enrollment, the instance operation gate, provider search/version IPC, instance creation, Evaluate/Prepare/Apply, durable idempotency/operation storage, operation cancellation/events, launch RPC, transaction journals and recovery. Every such request returns UNSUPPORTED_CAPABILITY with no operation created. No no-window QApplication backend has been implemented.
The write policy is approved: explicit enrollment for exclusive use by gate-aware Launcher versions. Enrollment itself remains unavailable. Do not edit live files from Manager.

## Prerequisites for a future writable integration
- Migrate relevant writers and launch preparation onto one cross-process physical-instance gate, including overlapping game roots. Existing processes and detached Java must be verified or block writes.
- Quiesce active/queued native tasks, flush and check earlier saves, then invalidate/reload models without replaying stale state.
- Adapt ResourceAPI plus ModrinthAPI/FlameAPI. ResourceDownloadTask currently downloads into the supplied live model directory; Prepare must instead use private persistent staging. Respect unavailable downloads, API permissions, dependency failures and rate limits; never export keys.
- Use PackProfile and Packwiz serializers for staged metadata. Implement reviewed digests, exact hash baselines, backups, durable operation records and per-file recovery intent/results before exposing Apply.
- Recover before ordinary startup scans/saves/launch, without Manager. Preserve uncertain backups and block launch after rollback failure.
- Prove a no-window QApplication lifecycle and an interaction-required policy separately; do not replace QApplication with QCoreApplication.

Relevant source: InstanceList::discoverInstances; INISettingsObject::doSave (immediate); PackProfile::scheduleSave/save_internal (5-second timer); ResourceFolderModel::update/resolveResource (queued reruns and resolver tasks); ResourceFolderLoadTask (renames resources and removes orphan metadata); LocalResourceUpdateTask; LauncherPartLaunch (detachable Java).
This status bridge does not implement the full writable task. Missing gate/quiescence/transaction work is not permission for Manager to bypass those safeguards.

## Review and validation
Compare baseline 6d91edbe0f168c18a334811d7b08e1b6ce89ef16 with feat/modpack-manager-integration. Build AwakeLauncher, AwakeIntegration and AwakeWebBridge using the repository's Windows CMake configuration. Run CTest; GUI/hardware tests require separate execution.
Verified on Windows with MSVC 2022 and Qt 6.11.2:
- Full native build passed. CTest passed 38/38 tests with `ctest --test-dir build --parallel 1 -E '^AwakeWebShell$' --output-on-failure`; QApplication fixtures ran offscreen. Individual live-auth/hardware skips remain skips.
- Integration fixtures covered malformed/fractured frames, negotiation/reconnect, same-profile ownership across processes, public-write ACL rejection, identity collisions, real symlinks/junctions, stale metadata, 305 mods, native index metadata filtering and relative `--dir` discovery. All instance data was temporary.
- Frontend unit tests passed 50/50. Captured native responses passed 11 JSON Schema checks. The independent review's response-schema mismatch was fixed and covered by a fixture regression.

Not executed: the visible AwakeWebShell suite, real Minecraft launches, live sign-in, hardware selection, cross-user/remote IPC attempts, or writable-operation/crash-recovery fault tests. No gate, Apply or recovery capability is implemented, so these results do not establish safe external mutation. Test tools/logs stay outside the committed product; no validator dependency was added to Launcher.
