# Native Core 0.4.1 delivery (1ae3218cdd8b)

Development delivery for the Awake Modpack Manager helper; `publicRelease` is false. Source commit `1ae3218cdd8b1ddd67e4e356638bc127a22d52b0` on `release/3.2.1`, protocol 2.0, extension `manager.v0.2.1`.

| File | SHA-256 |
|---|---|
| `awake-core-manager-v0.4.1-windows-x64.zip` | `dd70b9c0ddae4f1b95d0358ebf47ed9526da3aafd6b7c8fbee916598188cd555` |
| `awake-core-host.exe` (inside the ZIP) | `330b04a3d92407b3b20e7c9349c9d6bf3cb8ec6a79360b9645cf6936b83ca7c3` |
| `SHA256SUMS` (inside the ZIP) | `ae3db0ed62d3809b7d6538cbd83dc3d9b0afe036e27073ea4dd98ea0cb618eeb` |
| `awake-core-source-1ae3218cdd8b.zip` (107326584 bytes, reconstructed) | `df3db5f64fba207d469e7f87db01b757d6d5a867d74f1b8d7200bc5c7ab68b58` |
| `awake-core-source-1ae3218cdd8b.zip.001` (67108864 bytes) | `fef811450005f78ab5cee157680b7be73c6ac644f95bf15c945d489c7ea698d7` |
| `awake-core-source-1ae3218cdd8b.zip.002` (40217720 bytes) | `9a3f68898e37d96f0df1ad1de558f4dc8175988626c370fcde7b59e7d4359ed1` |

## Verify

```bash
sha256sum -c SHA256SUMS
python join_source.py ../awake-core-source-1ae3218cdd8b.zip
```

Every file here is committed byte-for-byte (`.gitattributes`: `native-core/redistribution/** -text`), so the outer
`SHA256SUMS` verifies on Windows and POSIX checkouts alike, including `core.autocrlf=true`. Extract the complete
`core-host/windows-x64` folder from the helper ZIP; the Manager stages it with `scripts/stage-helper.mjs`, which checks
the executable, the internal `SHA256SUMS`, the file set and `SOURCE.json`. The source ZIP is the complete corresponding
source (application tree, vcpkg tree and ports, dependency and Qt sources, notices); see `REDISTRIBUTION.md` inside it.
GitHub's automatic archive is not a substitute. Builds are not claimed reproducible.
