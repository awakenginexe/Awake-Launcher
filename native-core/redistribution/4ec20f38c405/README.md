# Native Core 0.3.0 delivery (4ec20f38c405)

Development delivery for the Awake Modpack Manager helper; `publicRelease` is false. Source commit `4ec20f38c4056b7cd1d2febf7c3b7b8bef8f944a` on `feat/native-core-manager-030`, protocol 2.0, extension `manager.v0.2.1`.

| File | SHA-256 |
|---|---|
| `awake-core-manager-v0.3.0-windows-x64.zip` | `ba79e49624a726402dcc9461b6aec1d2a76c6cb9899c9aab41557fdad4e142db` |
| `awake-core-host.exe` (inside the ZIP) | `2e8c6261f53d86a255ea5275a65275cb35c5326498a614d8d5b096dfa9bdd65b` |
| `SHA256SUMS` (inside the ZIP) | `ad75ab1fdebc86cc48ea8321db6adbdc298e01630b0d6cffb1d2e4a7d1c7bf23` |
| `awake-core-source-4ec20f38c405.zip` (107310335 bytes, reconstructed) | `f1e2b0363fbc87baa9b2228aa46a8ddc93d6faf4c8ced6e2ad024e7510a34f78` |
| `awake-core-source-4ec20f38c405.zip.001` (67108864 bytes) | `7dfe13a10d4047823e67239fc93996a902463243bcbdd312062d62a55967649c` |
| `awake-core-source-4ec20f38c405.zip.002` (40201471 bytes) | `99eb96ce27859bd668f6e5bf4252bb4b642d245d880d72cb485d290534acf3ce` |

## Verify

```bash
sha256sum -c SHA256SUMS
python join_source.py ../awake-core-source-4ec20f38c405.zip
```

Every file here is committed byte-for-byte (`.gitattributes`: `native-core/redistribution/** -text`), so the outer
`SHA256SUMS` verifies on Windows and POSIX checkouts alike, including `core.autocrlf=true`. Extract the complete
`core-host/windows-x64` folder from the helper ZIP; the Manager stages it with `scripts/stage-helper.mjs`, which checks
the executable, the internal `SHA256SUMS`, the file set and `SOURCE.json`. The source ZIP is the complete corresponding
source (application tree, vcpkg tree and ports, dependency and Qt sources, notices); see `REDISTRIBUTION.md` inside it.
GitHub's automatic archive is not a substitute. Builds are not claimed reproducible.
