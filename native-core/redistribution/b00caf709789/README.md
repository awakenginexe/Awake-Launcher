# Native Core 0.2.0 final helper and source

Executable/source pin: `b00caf7097897ab60ad0aa247e4964ece667976c`. The package is for Claude's Manager integration; no Launcher or Manager release is published by this branch.

- `awake-core-manager-v0.2.0-windows-x64.zip`: complete Windows x64 helper package, SHA-256 `2286079e486b4f067b946121ce7b997a15e985e8ec9598082b13b4995af0b37c`.
- `awake-core-source-b00caf709789.zip.001` and `.002`: ordered parts of the complete corresponding-source ZIP (split solely for GitHub's 100 MiB file limit).
- Complete source ZIP SHA-256: `3352fe74952dba6dc37c6287df7c5f397312d9f39bae84c5a245aaa7561c64ad`.
- All executable, package-manifest and part hashes are in `delivery.json`.

From a clone of this branch, run Python 3.11+: `python native-core/redistribution/b00caf709789/join_source.py <new-output-source.zip>`. Both parts and the reconstructed ZIP are SHA-256 verified. Extract that ZIP and follow `BUILD-AND-REDISTRIBUTE.md`; no credentials are needed. The corresponding source includes application sources, matching Qt, vcpkg/tool sources, selected ports/patches, dependency sources/notices and build inputs. Reproducible builds have not been independently proven.

For the Manager release, upload the **complete reconstructed source ZIP**, its `.sha256` file, the helper ZIP and its `.sha256` alongside the installer. Keep the helper's `licenses/`, `COPYING.md`, `REDISTRIBUTION.md`, `SOURCE.json` and `SHA256SUMS` in the installer. Re-pin Manager from `delivery.json`; update its schema/types as described in [handoff](../../../docs/modpack-manager/handoff.md). The source pin deliberately precedes this artifact/handoff commit to avoid recursively embedding the source archive in itself.
