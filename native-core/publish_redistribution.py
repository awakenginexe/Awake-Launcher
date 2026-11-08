# SPDX-License-Identifier: GPL-3.0-only
"""Publish a package_release.py output as a reproducible repository delivery directory.

Every text file is written as exact UTF-8/LF bytes and the outer SHA256SUMS is computed
over those bytes. .gitattributes marks native-core/redistribution/** -text, so a checkout
on any platform (including core.autocrlf=true) reproduces the hashed bytes. The complete
source ZIP is split into ordered parts below GitHub's per-file limit; join_source.py
verifies each part and the reconstructed ZIP.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

JOIN_SOURCE = """# SPDX-License-Identifier: GPL-3.0-only
import hashlib, json, shutil, sys
from pathlib import Path
root = Path(__file__).resolve().parent
manifest = json.loads((root / 'delivery.json').read_text())
for part in manifest['sourceParts']:
    with (root / part['file']).open('rb') as stream:
        assert hashlib.file_digest(stream, 'sha256').hexdigest() == part['sha256'], part['file']
destination = Path(sys.argv[1])
with destination.open('xb') as output:
    for part in manifest['sourceParts']:
        with (root / part['file']).open('rb') as stream:
            shutil.copyfileobj(stream, output)
with destination.open('rb') as stream:
    assert hashlib.file_digest(stream, 'sha256').hexdigest() == manifest['sourceArchiveSha256']
print('Verified corresponding-source ZIP:', destination.resolve())
"""


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def text(path, value):
    path.write_bytes(value.replace('\r\n', '\n').encode('utf-8'))


def readme(delivery, parts):
    lines = [
        f"# Native Core {delivery['implementationVersion']} delivery ({delivery['sourceCommit'][:12]})",
        '',
        f"Development delivery for the Awake Modpack Manager helper; `publicRelease` is false. Source commit `{delivery['sourceCommit']}` "
        f"on `{delivery['sourceBranch']}`, protocol {delivery['protocol']['major']}.{delivery['protocol']['minor']}, extension `{delivery['extension']}`.",
        '',
        '| File | SHA-256 |',
        '|---|---|',
        f"| `{delivery['helperZip']}` | `{delivery['helperZipSha256']}` |",
        f"| `awake-core-host.exe` (inside the ZIP) | `{delivery['executableSha256']}` |",
        f"| `SHA256SUMS` (inside the ZIP) | `{delivery['sumsSha256']}` |",
        f"| `{delivery['sourceArchive']}` ({delivery['sourceZipBytes']} bytes, reconstructed) | `{delivery['sourceArchiveSha256']}` |",
    ]
    lines += [f"| `{part['file']}` ({part['size']} bytes) | `{part['sha256']}` |" for part in parts]
    lines += [
        '',
        '## Verify',
        '',
        '```bash',
        'sha256sum -c SHA256SUMS',
        f"python join_source.py ../{delivery['sourceArchive']}",
        '```',
        '',
        'Every file here is committed byte-for-byte (`.gitattributes`: `native-core/redistribution/** -text`), so the outer',
        '`SHA256SUMS` verifies on Windows and POSIX checkouts alike, including `core.autocrlf=true`. Extract the complete',
        '`core-host/windows-x64` folder from the helper ZIP; the Manager stages it with `scripts/stage-helper.mjs`, which checks',
        'the executable, the internal `SHA256SUMS`, the file set and `SOURCE.json`. The source ZIP is the complete corresponding',
        'source (application tree, vcpkg tree and ports, dependency and Qt sources, notices); see `REDISTRIBUTION.md` inside it.',
        'GitHub\'s automatic archive is not a substitute. Builds are not claimed reproducible.',
        '',
    ]
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package-output', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    parser.add_argument('--part-size', type=int, default=64 * 1024 * 1024)
    args = parser.parse_args()
    source = args.package_output.resolve()
    built = json.loads((source / 'delivery.json').read_text(encoding='utf-8'))
    destination = args.destination.resolve()
    destination.mkdir(parents=True, exist_ok=False)
    helper_zip, source_zip = source / built['helperZip'], source / built['sourceArchive']
    if digest(helper_zip) != built['helperZipSha256'] or digest(source_zip) != built['sourceArchiveSha256']:
        raise ValueError('Package output differs from its delivery record')
    shutil.copyfile(helper_zip, destination / helper_zip.name)
    text(destination / (helper_zip.name + '.sha256'), f"{built['helperZipSha256']}  {helper_zip.name}\n")
    text(destination / (source_zip.name + '.sha256'), f"{built['sourceArchiveSha256']}  {source_zip.name}\n")
    parts = []
    with source_zip.open('rb') as stream:
        index = 0
        while chunk := stream.read(args.part_size):
            index += 1
            name = f'{source_zip.name}.{index:03d}'
            (destination / name).write_bytes(chunk)
            parts.append({'file': name, 'sha256': hashlib.sha256(chunk).hexdigest(), 'size': len(chunk)})
    delivery = {key: value for key, value in built.items() if key != 'helperDirectory'}
    delivery['sourceParts'] = parts
    text(destination / 'delivery.json', json.dumps(delivery, indent=2) + '\n')
    text(destination / 'join_source.py', JOIN_SOURCE)
    text(destination / 'README.md', readme(delivery, parts))
    files = sorted(path for path in destination.iterdir() if path.is_file() and path.name != 'SHA256SUMS')
    text(destination / 'SHA256SUMS', ''.join(f'{digest(path)}  {path.name}\n' for path in files))
    # Self-check: the manifest, the part hashes and the reconstructed archive.
    for line in (destination / 'SHA256SUMS').read_bytes().decode().splitlines():
        expected, name = line.split('  ', 1)
        assert digest(destination / name) == expected, name
    joined = hashlib.sha256()
    for part in parts:
        joined.update((destination / part['file']).read_bytes())
    assert joined.hexdigest() == built['sourceArchiveSha256']
    assert all(b'\r' not in (destination / name).read_bytes() for name in ('SHA256SUMS', 'delivery.json', 'join_source.py', 'README.md'))
    print(json.dumps({'destination': str(destination), 'files': [path.name for path in files] + ['SHA256SUMS']}, indent=2))


if __name__ == '__main__':
    main()
