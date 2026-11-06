# Standalone native helper protocol 2.0

Contract for `awake-core-host.exe`, Windows x64, role `modpack-core`. Launcher protocol 1.0 and its endpoint remain unchanged. This document defines the first Modrinth/Manager-owned workspace slice. Capability booleans describe actual availability; a schema is not an implemented capability. See core.schema.json for strict parameter and result definitions. Pending implementation capabilities remain false.

## Startup and transport
Package layout: `core-host/windows-x64/awake-core-host.exe`, sibling Qt Core/Network DLLs, qt.conf and required plugins. Tauri Rust resolves this trusted resource, uses CREATE_NO_WINDOW and `--profile <private absolute directory>`. The helper needs no Launcher installation, GUI or account. Use descriptor/hello readiness, not stdout EOF. Do not kill the helper on frontend timeout/disconnect/UI exit. It remains alive for active/prepared work and unresolved recovery. `core.shutdown` is refused until safe.

Descriptor: `<profile>/integration/core-v2/endpoint.json`. Pipe name `awake-core-v2-<session UUID>`. Verify pipe server PID and same-user SID, then protocol/role/profile/session; discovery files alone are not authentication. Server verifies local peer identity. Fixed local private profiles only; no remote clients. The profile coordinator lock prevents duplicate helpers. Independent profiles still share the canonical per-user physical ownership namespace. No caller may select its lock namespace.

Frames: big-endian uint32 byte length plus UTF-8 JSON object, 1–1,048,576 bytes. Eight clients, one request in flight per connection, no pipelining, 30-second idle timeout. Request `{protocol:{major:2,minor:0},requestId,method,params}`; response echoes protocol/requestId and exactly one of result/error. Unknown fields/types, unsafe identifiers and out-of-range values are rejected. Reconnect starts with hello. No credentials, arbitrary path writes/deletes or command execution.

## Methods
All result shapes and optional fields are in the schema; unsupported methods return UNSUPPORTED_CAPABILITY without creating an operation.

| Method | Purpose |
|---|---|
| hello | Offer versions/client; receive role, core/storage versions, profile/session, capabilities and limits. |
| core.info / core.shutdown | Resolved profile/state; request safe shutdown. |
| catalog.versions | Minecraft and supported loader choices; initially vanilla/Fabric, exact combinations validated. |
| workspaces.create / workspaces.list | Durable creation in helper-controlled storage; list enrolled Manager-owned physical workspaces. No existing-instance adoption. |
| providers.search / providers.versions | Modrinth reads, filtered by Minecraft/loader, bounded paging. Public artifact URLs/hashes; no secrets. |
| plans.prepare / plans.get | Resolve requested versions and required dependencies, stage verified files, preserve manual files, return paged exact review plan. |
| transactions.apply | Apply only the exact operationId/planDigest/instanceRevision reviewed by the user. |
| mods.list / updates.check | Hash-aware installed inventory; compatible update candidates. Updates require a new reviewed plan. |
| operations.get / operations.list / operations.cancel / events.poll | Durable status, coalesced progress snapshots, cancellation acknowledgement and reconnect. |
| recovery.list / recovery.run | Inspect retained recovery and request verified rollback. No arbitrary filesystem recovery. |
| inbox.begin / inbox.write / inbox.commit / inbox.status / outbox.read | Reserved bounded transfer tokens for future import/export; never live filesystem paths. No base64 archives in normal requests. |

Create produces native instance.cfg/mmc-pack.json plus a materialized `.minecraft/mods` tree. Name is display text; destination is backend chosen. The user must select Minecraft and loader versions. The helper does not launch Minecraft or authenticate accounts. Launcher adoption/sharing, CurseForge, arbitrary archives and export stay disabled until implemented safely.

Prepare changes: install/replace use Modrinth projectId/versionId; remove/enable/disable use inventoryId from the current revision. Required dependencies appear in review and are integrity-checked. Optional/embedded dependencies are reported without silently downloading extra files; incompatible or unsatisfied required versions block preparation. This is declared metadata checking, not a complete runtime compatibility guarantee. Unknown files are never pruned automatically. No UI 100-item limit applies.

## Durable operations and review
requestId correlates one exchange. idempotencyKey deduplicates side effects across restart; reusing a key with a different method/body is IDEMPOTENCY_CONFLICT. operationId identifies the persisted work. planDigest binds exact targets/hashes/metadata/dependencies and the expected resulting inventory. instanceRevision is an exact content baseline, checked under the gate immediately before writes. None of these fields substitute for each other.

Submission result is `{operationId,state,terminal,replayed}`. Fresh accepted work is nonterminal, never completed merely because it was queued. Follow operations.get; after a lost reply resend the identical request/key. Apply references the prepared operation and adds an idempotent authorization to it. The review plan is immutable and paged; fetch every page before confirmation. Prepared plans have no automatic expiry in this slice; explicitly cancel unused ones.

Prepare: queued → resolving → downloading → prepared (downloading may be skipped without artifacts). Apply: acquiring → revalidating → backing_up → applying → verifying → completed. Creation: queued → resolving → verifying → completed, without invented Apply phases. Rollback: rolling_back → rolled_back. Only completed/failed/cancelled/rolled_back are terminal; recovery_required is nonterminal and blocks unsafe use. Cancellation is acknowledged separately; permitted before Apply acquisition, never during replacement/recovery. Progress may coalesce. events.poll is a snapshot notification feed, not audit history; durable status is authoritative.

Download failure/cancellation never changes live files. Critical recovery data lives in persistent private operation storage, not startup-cleaned temp. Recovery precedes writes after restart. Unknown bytes/ownership, missing game evidence, unsupported record versions and unverifiable rollback retain evidence and fail closed. Multi-file replacement is recoverable, not globally atomic. A second writer cannot bypass the gate by selecting another registry/profile. Shared Launcher adoption is unavailable until all native writers/scans/saves/launch preparation honor the authority protocol.

Errors: INVALID_REQUEST, UNSUPPORTED_PROTOCOL, HANDSHAKE_REQUIRED, UNSUPPORTED_CAPABILITY, LIMIT_EXCEEDED, NOT_FOUND, NOT_ENROLLED, INCOMPATIBLE_OWNERSHIP, BUSY, STALE_INSTANCE, STALE_PLAN, IDEMPOTENCY_CONFLICT, DEPENDENCY_CONFLICT, POLICY_CONFLICT, PROVIDER_RESTRICTED, RATE_LIMITED, DOWNLOAD_FAILED, INTEGRITY_FAILED, CANCELLED, RECOVERY_REQUIRED, RECOVERY_FAILED, IO_ERROR, INTERNAL_ERROR. Each error has message and retryable. A retry never silently substitutes another version/plan.

## Client sequence
hello → catalog.versions → workspaces.create → operations.get until completed → providers.search/versions → mods.list → plans.prepare → operations.get until prepared → plans.get all pages → user review → transactions.apply → operations.get until verified terminal result → mods.list. Repeat preparation with explicit versions for updates. On recovery_required stop normal mutation and show recovery.list. Never fall back to direct writes or infer safety from connection loss.
