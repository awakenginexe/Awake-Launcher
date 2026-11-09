# Native Core 0.4.2 delivery (3127db450767)

Development delivery for the Awake Modpack Manager helper; `publicRelease` is false. Source commit `3127db4507671ae587a61cd75a6ee5da381e0274` on `release/3.2.2`, protocol 2.0, extension `manager.v0.2.1`.

| File | SHA-256 |
|---|---|
| `awake-core-manager-v0.4.2-windows-x64.zip` | `b022ae9512036ef72b44729ae74a6aab90cbded76aa354e5ca0d660a8c72fe2c` |
| `awake-core-host.exe` (inside the ZIP) | `79accdc4bc0082c1d99fb6d92c674d91ee13eb1f1468da36ca8cad7928e8ef22` |
| `SHA256SUMS` (inside the ZIP) | `1a7b932c3c769c6cf5c68e42b643ccd94e6da18bf9f9403409390ed8e65626c5` |
| `awake-core-source-3127db450767.zip` (107328189 bytes, reconstructed) | `bdb99425a1223491e15a9e9def46e0611a296212344ba5b73fd55b77083c7091` |
| `awake-core-source-3127db450767.zip.001` (67108864 bytes) | `9009bdc5501c30624c3131f70401d7cc1298689c5d2185093588576e48e8c651` |
| `awake-core-source-3127db450767.zip.002` (40219325 bytes) | `28b293e2924b9133ca3c6ad231aaef978aa8e3399c7176c2dbe3df7d53166951` |

## Verify

```bash
sha256sum -c SHA256SUMS
python join_source.py ../awake-core-source-3127db450767.zip
```

Every file here is committed byte-for-byte (`.gitattributes`: `native-core/redistribution/** -text`), so the outer
`SHA256SUMS` verifies on Windows and POSIX checkouts alike, including `core.autocrlf=true`. Extract the complete
`core-host/windows-x64` folder from the helper ZIP; the Manager stages it with `scripts/stage-helper.mjs`, which checks
the executable, the internal `SHA256SUMS`, the file set and `SOURCE.json`. The source ZIP is the complete corresponding
source (application tree, vcpkg tree and ports, dependency and Qt sources, notices); see `REDISTRIBUTION.md` inside it.
GitHub's automatic archive is not a substitute. Builds are not claimed reproducible.
