# SPDX-License-Identifier: GPL-3.0-only
"""Bundle a committed native helper, its notices, and exact corresponding source.

Run after deploy.ps1 in a fresh directory. Python 3.11+, Git and the recorded
source inputs are required; no network or credentials are used by this script.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def checked(path, expected):
    if digest(path) != expected:
        raise ValueError(f'Source hash mismatch: {path.name}')
    return path


def json_bytes(value):
    return (json.dumps(value, indent=2) + '\n').encode()


def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()


def archive_git(repo, commit, destination):
    subprocess.run(['git', '-C', str(repo), 'archive', '--format=tar', '-o', str(destination), commit], check=True)


def check_port(port, installed_share):
    # ABI records identify the recipe and patches used for the linked libraries.
    for line in (installed_share / 'vcpkg_abi_info.txt').read_text().splitlines():
        name, _, expected = line.partition(' ')
        if (port / name).is_file() and re.fullmatch('[0-9a-f]{64}', expected):
            checked(port / name, expected)


def verify_zip(path):
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)) or archive.testzip():
            raise ValueError('Duplicate or corrupt archive members')
        for line in archive.read('SHA256SUMS').decode().splitlines():
            expected, name = line.split('  ', 1)
            with archive.open(name) as stream:
                actual = hashlib.file_digest(stream, 'sha256').hexdigest()
            if expected != actual:
                raise ValueError(f'Archive content mismatch: {name}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('helper-dir', 'build-dir', 'qt-prefix', 'qt-source', 'vcpkg-root', 'registry-ports', 'downloads', 'dependency-prefix', 'triplet', 'tool-source', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--source-commit', required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    manifest = json.loads((root / 'native-core/source-dependencies.json').read_text())
    commit = git(root, 'rev-parse', args.source_commit + '^{commit}')
    tracked = set(git(root, 'ls-tree', '-r', '--name-only', commit).splitlines())
    if not {'native-core/package_release.py', 'native-core/source-dependencies.json', 'native-core/REDISTRIBUTION.md'} <= tracked:
        raise ValueError('The source commit omits required packaging inputs')
    if git(root, 'diff', '--name-only', commit, '--'):
        raise ValueError('Commit the complete source tree before packaging')
    if git(args.vcpkg_root, 'rev-parse', 'HEAD') != manifest['vcpkgCommit'] or git(args.vcpkg_root, 'status', '--porcelain'):
        raise ValueError('The vcpkg source tree differs from the recorded build input')
    checked(args.qt_source, manifest['qt']['sha256'])
    checked(args.tool_source, manifest['vcpkgTool']['sha256'])
    checked(args.triplet, manifest['tripletSha256'])
    sources = [checked(args.downloads / dep['archive'], dep['sha256']) for dep in manifest['dependencies']]
    build_sources = [checked(args.downloads / dep['archive'], dep['sha256']) for dep in manifest['buildSources']]
    helper = args.helper_dir.resolve()
    exe = helper / 'awake-core-host.exe'
    if not exe.is_file():
        raise ValueError('Deploy the complete helper directory first')
    checked(exe, digest(args.build_dir / 'awake-core-host.exe'))
    cache = (args.build_dir / 'CMakeCache.txt').read_text()
    if f'CMAKE_HOME_DIRECTORY:INTERNAL={(root / "native-core").as_posix()}' not in cache:
        raise ValueError('The helper build directory belongs to different sources')
    for name, expected in manifest['qt']['binarySha256'].items():
        source = args.qt_prefix / ('plugins' if '/' in name else 'bin') / name
        checked(source, expected)
        checked(helper / name, expected)
    output = args.output.resolve()
    output.mkdir(exist_ok=False, parents=True)
    source_name = f'awake-core-source-{commit[:12]}.zip'
    source_zip = output / source_name
    provenance = dict(sourceRepository='https://github.com/awakenginexe/Awake-Launcher', sourceCommit=commit,
                      sourceBranch='feat/native-core-manager-020', implementationVersion='0.2.0',
                      protocol={'major': 2, 'minor': 0}, extension='manager.v0.2', platform='windows-x64',
                      publicRelease=False, executableSha256=digest(exe), reproducibleBuildVerified=False)
    with tempfile.TemporaryDirectory(prefix='awake-source-') as scratch:
        scratch = Path(scratch)
        bundle = scratch / 'source'
        bundle.mkdir()
        archive_git(root, commit, bundle / 'awake-source.tar')
        archive_git(args.vcpkg_root, manifest['vcpkgCommit'], bundle / 'vcpkg-source.tar')
        for dep, archive in zip(manifest['dependencies'], sources):
            target = bundle / 'dependencies' / archive.name
            target.parent.mkdir(exist_ok=True)
            shutil.copyfile(archive, target)
            port = args.registry_ports / dep['portTree']
            if not port.is_dir() or json.loads((port / 'vcpkg.json').read_text())['name'] != dep['name']:
                raise ValueError(f'Missing exact vcpkg port: {dep["name"]}')
            shutil.copytree(port, bundle / 'ports' / dep['name'])
            share = args.dependency_prefix / 'share' / dep['name']
            check_port(port, share)
            shutil.copytree(share, bundle / 'dependency-build' / dep['name'])
            notice = helper / 'licenses' / (dep['name'] + '.txt')
            notice.parent.mkdir(exist_ok=True)
            shutil.copyfile(share / 'copyright', notice)
        for port_name, tree in manifest['buildPorts'].items():
            port = args.registry_ports / tree
            check_port(port, args.dependency_prefix / 'share' / port_name)
            shutil.copytree(port, bundle / 'ports' / port_name)
        check_port(root / 'cmake/vcpkg-ports/vcpkg-tool-meson', args.dependency_prefix / 'share/vcpkg-tool-meson')
        shutil.copytree(root / 'cmake/vcpkg-ports/vcpkg-tool-meson', bundle / 'ports/vcpkg-tool-meson')
        for archive in build_sources:
            shutil.copyfile(archive, bundle / 'dependencies' / archive.name)
        for archive in (args.qt_source, args.tool_source):
            shutil.copyfile(archive, bundle / 'dependencies' / archive.name)
        triplets = bundle / 'triplets'
        triplets.mkdir()
        shutil.copyfile(args.triplet, triplets / 'x64-windows.cmake')
        qtbuild = bundle / 'qt-build'
        qtbuild.mkdir()
        for path in sorted((args.qt_prefix / 'sbom').glob('qtbase-*')):
            shutil.copyfile(path, qtbuild / path.name)
        shutil.copyfile(args.qt_prefix / 'config_qtbase.summary', qtbuild / 'config_qtbase.summary')
        shutil.copyfile(args.build_dir / 'CMakeCache.txt', bundle / 'helper-CMakeCache.txt')
        # Keep all Qt notices: bundled third-party sources have additional terms.
        with tarfile.open(args.qt_source) as archive:
            for member in archive:
                relative = PurePosixPath(member.name)
                if not member.isfile() or '..' in relative.parts:
                    continue
                relative = PurePosixPath(*relative.parts[1:])
                name = relative.name.upper()
                if relative.parts[0] == 'LICENSES' or name.startswith(('LICENSE', 'LICENCE', 'COPYING', 'COPYRIGHT', 'NOTICE')) or name in ('QT_ATTRIBUTION.JSON', 'README.QT'):
                    target = helper / 'licenses/qt' / Path(str(relative))
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with archive.extractfile(member) as stream:
                        target.write_bytes(stream.read())
        shutil.copyfile(args.qt_prefix / 'sbom/qtbase-6.11.2.spdx.json', helper / 'licenses/qt/qtbase-binary.spdx.json')
        shutil.copyfile(root / 'LICENSE', helper / 'licenses/GPL-3.0.txt')
        shutil.copyfile(root / 'native-core/REDISTRIBUTION.md', helper / 'REDISTRIBUTION.md')
        shutil.copyfile(root / 'native-core/REDISTRIBUTION.md', bundle / 'BUILD-AND-REDISTRIBUTE.md')
        shutil.copyfile(root / 'native-core/source-dependencies.json', bundle / 'source-dependencies.json')
        dependency_manifest = dict(name='awake-native-core-dependencies', **{'version-string': '0.2.0'}, dependencies=[
            {'name':'libarchive', 'default-features':False, 'features':['bzip2','lz4','lzma','lzo','zstd']}, 'tomlplusplus', 'zlib'])
        (bundle / 'vcpkg.json').write_bytes(json_bytes(dependency_manifest))
        (bundle / 'SOURCE.json').write_bytes(json_bytes(provenance))
        files = sorted(path for path in bundle.rglob('*') if path.is_file())
        (bundle / 'SHA256SUMS').write_text(''.join(f'{digest(path)}  {path.relative_to(bundle).as_posix()}\n' for path in files), encoding='utf-8', newline='\n')
        with zipfile.ZipFile(source_zip, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for path in sorted(path for path in bundle.rglob('*') if path.is_file()):
                archive.write(path, path.relative_to(bundle).as_posix(), compress_type=zipfile.ZIP_STORED if path.name.endswith('.tar.gz') else zipfile.ZIP_DEFLATED)
    verify_zip(source_zip)
    provenance.update(sourceArchive=source_name, sourceArchiveSha256=digest(source_zip))
    (helper / 'SOURCE.json').write_bytes(json_bytes(provenance))
    package_files = sorted(path for path in helper.rglob('*') if path.is_file() and path.name != 'SHA256SUMS')
    (helper / 'SHA256SUMS').write_text(''.join(f'{digest(path)}  {path.relative_to(helper).as_posix()}\n' for path in package_files), encoding='utf-8', newline='\n')
    package_zip = output / 'awake-core-manager-v0.2.0-windows-x64.zip'
    with zipfile.ZipFile(package_zip, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(path for path in helper.rglob('*') if path.is_file()):
            archive.write(path, 'core-host/windows-x64/' + path.relative_to(helper).as_posix())
    with zipfile.ZipFile(package_zip) as archive:
        assert archive.testzip() is None
        assert archive.read('core-host/windows-x64/awake-core-host.exe') == exe.read_bytes()
        for line in archive.read('core-host/windows-x64/SHA256SUMS').decode().splitlines():
            expected, name = line.split('  ', 1)
            assert hashlib.sha256(archive.read('core-host/windows-x64/' + name)).hexdigest() == expected
    delivery = dict(provenance, helperDirectory=str(helper), helperZip=package_zip.name, helperZipSha256=digest(package_zip),
                    sumsSha256=digest(helper / 'SHA256SUMS'), sourceZipBytes=source_zip.stat().st_size)
    for path in (package_zip, source_zip):
        path.with_suffix(path.suffix + '.sha256').write_text(f'{digest(path)}  {path.name}\n', encoding='utf-8')
    (output / 'delivery.json').write_bytes(json_bytes(delivery))
    print(json.dumps(delivery, indent=2))


if __name__ == '__main__':
    main()
