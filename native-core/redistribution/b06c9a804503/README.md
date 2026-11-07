# Native Core 0.2.1 delivery (b06c9a804503)

Development delivery for the Awake Modpack Manager helper; `publicRelease` is false. Source commit `b06c9a804503545207bacfa3bfdf8bfe23604e07` on `feat/native-core-manager-020`, protocol 2.0, extension `manager.v0.2.1`.

| File | SHA-256 |
|---|---|
| `awake-core-manager-v0.2.1-windows-x64.zip` | `0aea4ce7f3adcd7fd920d55c4b751623c5846be24331b2846e9624b026bc6792` |
| `awake-core-host.exe` (inside the ZIP) | `a71b35e946ffef00bed4b1200c4796e7d16001dac8089481d0192035ae64124b` |
| `SHA256SUMS` (inside the ZIP) | `aacd3d97188c45aa63b1f9a290d0c1d39a531c74e1126e7ef044e8c81116f752` |
| `awake-core-source-b06c9a804503.zip` (107287939 bytes, reconstructed) | `543dcc404bdb9bb6f319dceb6e860b1040a7c55611fba5443d934444237503ee` |
| `awake-core-source-b06c9a804503.zip.001` (67108864 bytes) | `f92b4b609fa4444d4ae6bfdda2c0455c1ea3978c74916188abb6407721d38f67` |
| `awake-core-source-b06c9a804503.zip.002` (40179075 bytes) | `b1c07c9c33a10b159f08644f55eb1515587b956c524b44864cbfb841c1750b41` |

## Verify

```bash
sha256sum -c SHA256SUMS
python join_source.py ../awake-core-source-b06c9a804503.zip
```

Every file here is committed byte-for-byte (`.gitattributes`: `native-core/redistribution/** -text`), so the outer
`SHA256SUMS` verifies on Windows and POSIX checkouts alike, including `core.autocrlf=true`. Extract the complete
`core-host/windows-x64` folder from the helper ZIP; the Manager stages it with `scripts/stage-helper.mjs`, which checks
the executable, the internal `SHA256SUMS`, the file set and `SOURCE.json`. The source ZIP is the complete corresponding
source (application tree, vcpkg tree and ports, dependency and Qt sources, notices); see `REDISTRIBUTION.md` inside it.
GitHub's automatic archive is not a substitute. Builds are not claimed reproducible.
