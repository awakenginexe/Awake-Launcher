# SPDX-License-Identifier: GPL-3.0-only
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
