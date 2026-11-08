# Native Core 0.3.0 delivery (37d708d14871)

Development delivery for the Awake Modpack Manager helper; `publicRelease` is false. Source commit `37d708d148715b1d3ea1cf689c00cfe8fc72a990` on `feat/native-core-manager-030`, protocol 2.0, extension `manager.v0.2.1`.

| File | SHA-256 |
|---|---|
| `awake-core-manager-v0.3.0-windows-x64.zip` | `0a4bdd5e9a34191934024cee4eb9c1356f6cdd812a7dc4c66b0fccf7a3f45e0a` |
| `awake-core-host.exe` (inside the ZIP) | `a40929248d077b2432b8a0c4d9067406a2f6ec2ed2097bb8393464e2a32c17b1` |
| `SHA256SUMS` (inside the ZIP) | `c8ad8df580b6c766c0efaf52c7132a71b774abf73eb73cfb8f0784dc3207810c` |
| `awake-core-source-37d708d14871.zip` (107310047 bytes, reconstructed) | `7d1195f94b5b03b143b28dd8eb6681b63dbe62ac7eb6e16df762f5c179ae1523` |
| `awake-core-source-37d708d14871.zip.001` (67108864 bytes) | `dce47280867c60233ff1b6a4d29a15dd040ca29618aac987e35feed531496fcd` |
| `awake-core-source-37d708d14871.zip.002` (40201183 bytes) | `4ed0455df2230238f55e755b8bbc1e3cd6f0a44678294487ffffab37f426d8aa` |

## Verify

```bash
sha256sum -c SHA256SUMS
python join_source.py ../awake-core-source-37d708d14871.zip
```

Every file here is committed byte-for-byte (`.gitattributes`: `native-core/redistribution/** -text`), so the outer
`SHA256SUMS` verifies on Windows and POSIX checkouts alike, including `core.autocrlf=true`. Extract the complete
`core-host/windows-x64` folder from the helper ZIP; the Manager stages it with `scripts/stage-helper.mjs`, which checks
the executable, the internal `SHA256SUMS`, the file set and `SOURCE.json`. The source ZIP is the complete corresponding
source (application tree, vcpkg tree and ports, dependency and Qt sources, notices); see `REDISTRIBUTION.md` inside it.
GitHub's automatic archive is not a substitute. Builds are not claimed reproducible.
