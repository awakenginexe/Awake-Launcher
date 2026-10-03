# Upstream

Awake Launcher is based on [Prism Launcher](https://github.com/PrismLauncher/PrismLauncher).

- Branch: `develop`
- Audited revision: `323609694d04a4715e88a087ccf7de05696c998c`
- Import date: 2026-10-03
- vcpkg: `908da3a305a0a8028d9602ab241b433652b3df69`
- libnbtplusplus: `3538933614059f0f44388a2b16f3db25ce42285b`

The import is a merge retaining upstream history and the original Awake repository commit. `origin` points to Awake; `upstream` points to Prism. Develop work on `feature/awake-foundation`.

## Maintenance

Fetch with `git fetch --no-tags upstream develop`. Review upstream changes before merging or cherry-picking them onto a dedicated maintenance branch. Never force-push or rewrite the imported history. Resolve branding and service configuration deliberately; upstream credentials, update feeds and release workflows must not replace Awake configuration.

Awake additions should live under `launcher/awake/` where possible. Changes to upstream files should remain small integration points. Preserve settings, instance formats, downloads, metadata and launch semantics unless a separately tested change is necessary.

The owner requirements are recorded in [the project brief](docs/PROJECT-BRIEF.md). Architecture and workflow notes are maintained locally, outside the tracked product source.

