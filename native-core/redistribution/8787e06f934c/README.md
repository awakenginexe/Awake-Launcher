# Native Core 0.4.0 delivery (8787e06f934c)

Development delivery for the Awake Modpack Manager helper; `publicRelease` is false. Source commit `8787e06f934c6f792d102d7ab885d8afffdccb12` on `feat/native-core-manager-040`, protocol 2.0, extension `manager.v0.2.1`.

| File | SHA-256 |
|---|---|
| `awake-core-manager-v0.4.0-windows-x64.zip` | `76cb2078d065efeb0a80b230cbed794d04ac2079ee1cd70973b0b56a616ab7b5` |
| `awake-core-host.exe` (inside the ZIP) | `4e0c761a9550d3236ace200d38676c729075d509f9d828577d5d527b5cfe9a6b` |
| `SHA256SUMS` (inside the ZIP) | `f10ff14db549ccbabba267794c882330e3711561208c479498c2a2a0fdcfe319` |
| `awake-core-source-8787e06f934c.zip` (107323839 bytes, reconstructed) | `55e57c82b76ee026350817647d1be32838d77f8db196de9b4404f87956f76c2b` |
| `awake-core-source-8787e06f934c.zip.001` (67108864 bytes) | `2ef496d2cbc0fa6883160b5ca276ac2f170122054dff350796b77d6eaf23f596` |
| `awake-core-source-8787e06f934c.zip.002` (40214975 bytes) | `cccb8cd93595431be3bfad50e30ed71f18a19d6f5fdc6956c3b00146464a7523` |

## Verify

```bash
sha256sum -c SHA256SUMS
python join_source.py ../awake-core-source-8787e06f934c.zip
```

Every file here is committed byte-for-byte (`.gitattributes`: `native-core/redistribution/** -text`), so the outer
`SHA256SUMS` verifies on Windows and POSIX checkouts alike, including `core.autocrlf=true`. Extract the complete
`core-host/windows-x64` folder from the helper ZIP; the Manager stages it with `scripts/stage-helper.mjs`, which checks
the executable, the internal `SHA256SUMS`, the file set and `SOURCE.json`. The source ZIP is the complete corresponding
source (application tree, vcpkg tree and ports, dependency and Qt sources, notices); see `REDISTRIBUTION.md` inside it.
GitHub's automatic archive is not a substitute. Builds are not claimed reproducible.
