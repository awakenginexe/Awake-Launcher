"""Windows-only, disposable-profile tests for a packaged awake-core-host.

Requires Python 3.11+ and jsonschema. --live explicitly permits public Modrinth
downloads. Never points at a user profile or terminates an unrelated process.
"""
import argparse
import csv
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import msvcrt
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import tomllib
import uuid
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--helper', type=Path, required=True)
parser.add_argument('--fixture-executable', type=Path)
parser.add_argument('--live', action='store_true')
parser.add_argument('--output', type=Path)
parser.add_argument('--schema-tools', type=Path)
args = parser.parse_args()
if args.schema_tools:
    sys.path.insert(0, str(args.schema_tools))
from jsonschema import Draft202012Validator, FormatChecker

ROOT = Path(__file__).resolve().parents[2]
SCHEMA = json.loads((ROOT / 'docs/modpack-manager/core.schema.json').read_text())
PROTOCOL = {'major': 2, 'minor': 0}
RUN = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix='awake-core-ipc-'))
if args.output:
    RUN.mkdir(exist_ok=False)
sid = next(csv.reader(subprocess.check_output(['whoami', '/user', '/fo', 'csv', '/nh'], text=True).splitlines()))[1]
subprocess.run(['icacls', str(RUN), '/inheritance:r', '/grant:r', '*' + sid + ':(OI)(CI)F',
                '*S-1-5-18:(OI)(CI)F', '*S-1-5-32-544:(OI)(CI)F'], check=True, capture_output=True)
ENV = dict(os.environ)
for key in list(ENV):
    if key.startswith(('QT_', 'QML', 'AWAKE', 'PRISM')):
        ENV.pop(key)
ENV['PATH'] = os.environ['SystemRoot'] + r'\System32;' + os.environ['SystemRoot']
K = C.WinDLL('kernel32', use_last_error=True)
A = C.WinDLL('advapi32', use_last_error=True)
K.PeekNamedPipe.argtypes = [W.HANDLE, W.LPVOID, W.DWORD, C.POINTER(W.DWORD), C.POINTER(W.DWORD), C.POINTER(W.DWORD)]
K.PeekNamedPipe.restype = W.BOOL
K.GetNamedPipeServerProcessId.argtypes = [W.HANDLE, C.POINTER(W.ULONG)]
K.GetNamedPipeServerProcessId.restype = W.BOOL
K.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
K.OpenProcess.restype = W.HANDLE
K.GetCurrentProcess.restype = W.HANDLE
K.CloseHandle.argtypes = [W.HANDLE]
K.CreateFileW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, W.LPVOID, W.DWORD, W.DWORD, W.HANDLE]
K.CreateFileW.restype = W.HANDLE
A.OpenProcessToken.argtypes = [W.HANDLE, W.DWORD, C.POINTER(W.HANDLE)]
A.GetTokenInformation.argtypes = [W.HANDLE, C.c_int, W.LPVOID, W.DWORD, C.POINTER(W.DWORD)]
A.EqualSid.argtypes = [W.LPVOID, W.LPVOID]
A.EqualSid.restype = W.BOOL
P = C.WinDLL('psapi', use_last_error=True)
P.EnumProcessModulesEx.argtypes = [W.HANDLE, C.POINTER(W.HMODULE), W.DWORD, C.POINTER(W.DWORD), W.DWORD]
P.EnumProcessModulesEx.restype = W.BOOL
P.GetModuleFileNameExW.argtypes = [W.HANDLE, W.HMODULE, W.LPWSTR, W.DWORD]
P.GetModuleFileNameExW.restype = W.DWORD
PROCESSES = []
RESULTS = []


def passed(name, **evidence):
    RESULTS.append({'test': name, **evidence})
    print('PASS ' + name + ' ' + json.dumps(evidence), flush=True)
    (RUN / 'results.json').write_text(json.dumps(RESULTS, indent=2))


def validate(definition, value):
    Draft202012Validator({'$ref': '#/$defs/' + definition, '$defs': SCHEMA['$defs']}, format_checker=FormatChecker()).validate(value)


def token_user(process):
    token = W.HANDLE()
    assert A.OpenProcessToken(process, 8, C.byref(token)), C.get_last_error()
    try:
        size = W.DWORD()
        A.GetTokenInformation(token, 1, None, 0, C.byref(size))
        buffer = C.create_string_buffer(size.value)
        assert A.GetTokenInformation(token, 1, buffer, size, C.byref(size)), C.get_last_error()
        return buffer
    finally:
        K.CloseHandle(token)


def verify_modules(helper):
    modules = (W.HMODULE * 1024)()
    needed = W.DWORD()
    handle = W.HANDLE(int(helper.process._handle))
    assert P.EnumProcessModulesEx(handle, modules, C.sizeof(modules), C.byref(needed), 3)
    assert needed.value <= C.sizeof(modules)
    paths = []
    for module in modules[:needed.value // C.sizeof(W.HMODULE)]:
        text = C.create_unicode_buffer(32768)
        assert P.GetModuleFileNameExW(handle, module, text, len(text))
        paths.append(Path(text.value).resolve())
    qt = [p for p in paths if p.name.lower().startswith('qt6')]
    assert {p.name.lower() for p in qt} == {'qt6core.dll', 'qt6network.dll'}, qt
    package = args.helper.resolve().parent
    assert all(p.parent == package for p in qt)
    if (package / 'vcruntime140.dll').exists():
        crt = [p for p in paths if p.name.lower().startswith(('vcruntime140', 'msvcp140'))]
        assert crt and all(p.parent == package for p in crt), crt
    return [p.name for p in qt]


class Client:
    def __init__(self, descriptor):
        validate('descriptor', descriptor)
        self.file = open('\\\\.\\pipe\\' + descriptor['serverName'], 'r+b', buffering=0)
        self.handle = msvcrt.get_osfhandle(self.file.fileno())
        pid = W.ULONG()
        assert K.GetNamedPipeServerProcessId(self.handle, C.byref(pid)), C.get_last_error()
        assert str(pid.value) == descriptor['pid']
        process = K.OpenProcess(0x1000, False, pid.value)
        assert process
        try:
            own, peer = token_user(K.GetCurrentProcess()), token_user(process)
            assert A.EqualSid(C.cast(own, C.POINTER(W.LPVOID))[0], C.cast(peer, C.POINTER(W.LPVOID))[0])
        finally:
            K.CloseHandle(process)
        self.sequence = 0

    def close(self):
        self.file.close()

    def read(self, length):
        data = b''
        deadline = time.monotonic() + 30
        while len(data) < length:
            available = W.DWORD()
            if not K.PeekNamedPipe(self.handle, None, 0, None, C.byref(available), None):
                raise OSError(C.get_last_error(), 'Pipe disconnected')
            if available.value:
                chunk = self.file.read(min(length - len(data), available.value))
                if not chunk:
                    raise EOFError('Pipe closed')
                data += chunk
            elif time.monotonic() >= deadline:
                raise TimeoutError('IPC response deadline')
            else:
                time.sleep(.005)
        return data

    def call(self, method, params=None, error=None, protocol=None):
        self.sequence += 1
        body = {'protocol': protocol or PROTOCOL, 'requestId': 'fixture_' + str(self.sequence), 'method': method, 'params': params or {}}
        data = json.dumps(body, separators=(',', ':')).encode()
        self.file.write(struct.pack('>I', len(data)) + data)
        size, = struct.unpack('>I', self.read(4))
        assert 0 < size <= 1048576
        response = json.loads(self.read(size))
        validate('response', response)
        assert response['requestId'] == body['requestId'] and response['protocol'] == PROTOCOL
        if error:
            assert response['error']['code'] == error, response
            return response['error']
        assert 'result' in response, response
        validate(method + '.result', response['result'])
        return response['result']

    def hello(self):
        return self.call('hello', {'versions': [PROTOCOL], 'client': {'name': 'packaged-core-fixture', 'version': '1'}})

    def wait(self, operation, states=('completed',), timeout=180, allow_recovery=False):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            status = self.call('operations.get', {'operationId': operation})
            if status['state'] in states:
                return status
            assert not status['terminal'] and (allow_recovery or status['state'] != 'recovery_required'), status
            time.sleep(.05)
        raise TimeoutError('Operation status: ' + json.dumps(status))


class Helper:
    def __init__(self, profile, label):
        self.profile = profile
        self.output = (RUN / (label + '.log')).open('wb')
        self.process = subprocess.Popen([str(args.helper.resolve()), '--profile', str(profile)], cwd=RUN, env=ENV,
                                        stdout=self.output, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
        PROCESSES.append(self)
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            assert self.process.poll() is None, (label, self.process.returncode, (RUN / (label + '.log')).read_text(errors='replace'))
            path = profile / 'integration/core-v2/endpoint.json'
            try:
                descriptor = json.loads(path.read_text())
                if descriptor['pid'] == str(self.process.pid):
                    self.descriptor = descriptor
                    return
            except (FileNotFoundError, ValueError):
                pass
            time.sleep(.05)
        raise TimeoutError('Helper startup ' + label)

    def connect(self):
        client = Client(self.descriptor)
        hello = client.hello()
        for key in ('profileId', 'sessionId', 'role'):
            assert hello[key] == self.descriptor[key]
        return client

    def kill(self):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait(10)
        self.output.close()

    def shutdown(self, client):
        client.call('core.shutdown')
        client.close()
        assert self.process.wait(10) == 0
        self.output.close()


def submit(client, method, params):
    result = client.call(method, params)
    assert not result['replayed'] and not result['terminal'], result
    return result['operationId']


def prepare(client, workspace, changes):
    inventory = client.call('mods.list', {'physicalId': workspace['physicalId']})
    params = {'idempotencyKey': uuid.uuid4().hex, 'physicalId': workspace['physicalId'],
              'instanceRevision': inventory['instanceRevision'], 'changes': changes}
    op = submit(client, 'plans.prepare', params)
    client.wait(op, ('prepared',))
    first = client.call('plans.get', {'operationId': op, 'limit': 200})
    files = list(first['files'])
    cursor = first['nextOffset']
    while cursor is not None:
        page = client.call('plans.get', {'operationId': op, 'offset': cursor, 'limit': 200})
        assert page['planDigest'] == first['planDigest']
        files += page['files']
        cursor = page['nextOffset']
    assert len(files) == first['total']
    return op, first, params


def apply(client, operation, plan):
    params = {'idempotencyKey': uuid.uuid4().hex, 'operationId': operation, 'planDigest': plan['planDigest'], 'instanceRevision': plan['instanceRevision']}
    assert submit(client, 'transactions.apply', params) == operation
    final = client.wait(operation)
    assert final['result']['verified'] and final['result']['instanceRevision'] == plan['expectedInstanceRevision']
    replay = client.call('transactions.apply', params)
    assert replay['replayed'] and replay['state'] == 'completed'
    return final


def live_workflow(helper):
    client = helper.connect()
    catalog = client.call('catalog.versions', {'minecraftVersion': '1.21.1', 'loader': 'fabric'})
    assert '1.21.1' in catalog['minecraftVersions'] and catalog['loaderVersions']
    create = {'idempotencyKey': uuid.uuid4().hex, 'name': 'Disposable real Modrinth workflow', 'minecraftVersion': '1.21.1',
              'loader': {'id': 'fabric', 'version': catalog['loaderVersions'][0]}}
    creation = submit(client, 'workspaces.create', create)
    client.close()  # Work continues without the initiating connection.
    client = helper.connect()
    workspace = client.wait(creation)['result']['workspace']
    persisted = json.loads((helper.profile / 'operations' / creation / 'context.json').read_text())['body']['request']
    assert persisted == create, 'Dispatch must not insert null optional parameters into durable requests'
    replay = client.call('workspaces.create', create)
    assert replay['replayed'] and replay['operationId'] == creation
    assert client.call('workspaces.list')['total'] == 1
    assert client.call('operations.list')['total'] == 1
    event = client.call('events.poll', {'afterSequence': '0'})
    assert event['operations'] and int(event['nextSequence']) > 0
    assert client.call('events.poll', {'afterSequence': event['nextSequence']})['operations'] == []
    client.call('workspaces.create', {**create, 'name': 'conflicting key'}, error='IDEMPOTENCY_CONFLICT')
    root = helper.profile / 'workspaces' / workspace['workspaceId']
    native = json.loads((root / 'mmc-pack.json').read_text())
    assert native['components'][0]['version'] == '1.21.1' and (root / 'instance.cfg').is_file()
    passed('real native workspace creation, disconnect and idempotent replay', workspaceId=workspace['workspaceId'])
    search = client.call('providers.search', {'provider': 'modrinth', 'minecraftVersion': '1.21.1', 'loader': 'fabric', 'query': 'sodium extra'})
    assert any(p['slug'] == 'sodium-extra' for p in search['projects'])
    versions = client.call('providers.versions', {'provider': 'modrinth', 'minecraftVersion': '1.21.1', 'loader': 'fabric', 'projectId': 'sodium-extra'})['versions']
    releases = [v for v in versions if v['releaseType'] == 'release']
    assert len(releases) >= 2
    old, new = releases[1], releases[0]
    op, plan, _ = prepare(client, workspace, [{'action': 'install', 'projectId': old['projectId'], 'versionId': old['versionId']}])
    assert plan['dependencies']
    client.call('core.shutdown', error='BUSY')
    apply(client, op, plan)
    inventory = client.call('mods.list', {'physicalId': workspace['physicalId']})
    assert len(inventory['items']) >= 2 and all(v['metadataState'] == 'verified' for v in inventory['items'])
    for item in inventory['items']:
        path = root / '.minecraft/mods' / item['fileName']
        assert hashlib.sha256(path.read_bytes()).hexdigest() == item['sha256'] and zipfile.is_zipfile(path)
        metadata = tomllib.loads((root / '.minecraft/mods/.index' / ('modrinth-' + item['projectId'] + '.pw.toml')).read_text())
        assert metadata['update']['modrinth']['version'] == item['versionId']
    updates = client.call('updates.check', {'physicalId': workspace['physicalId']})
    assert updates['instanceRevision'] == inventory['instanceRevision']
    assert next(v for v in updates['updates'] if v['projectId'] == old['projectId'])['candidate']['versionId'] == new['versionId']
    passed('real Modrinth installation and required dependencies over IPC', versions=[(v['projectId'], v['versionId']) for v in inventory['items']], revision=inventory['instanceRevision'])
    changes = [{'action': 'replace', 'projectId': new['projectId'], 'versionId': new['versionId']}]
    op, plan, _ = prepare(client, workspace, changes)
    manual = root / '.minecraft/mods/manual.jar'
    manual.write_bytes(b'preserve explicitly installed unknown bytes')
    submit(client, 'transactions.apply', {'idempotencyKey': uuid.uuid4().hex, 'operationId': op, 'planDigest': plan['planDigest'], 'instanceRevision': plan['instanceRevision']})
    client.wait(op, ('failed',))
    assert manual.read_bytes() == b'preserve explicitly installed unknown bytes'
    op, plan, _ = prepare(client, workspace, changes)
    apply(client, op, plan)
    passed('real version update and stale-plan rejection', fromVersion=old['versionId'], toVersion=new['versionId'])
    for action in ('disable', 'enable'):
        items = client.call('mods.list', {'physicalId': workspace['physicalId']})['items']
        selected = next(v for v in items if v['projectId'] == new['projectId'])
        op, plan, _ = prepare(client, workspace, [{'action': action, 'inventoryId': selected['inventoryId']}])
        apply(client, op, plan)
        items = client.call('mods.list', {'physicalId': workspace['physicalId']})['items']
        assert next(v for v in items if v['projectId'] == new['projectId'])['enabled'] == (action == 'enable')
    passed('real mod disable and enable with verified metadata')
    op, plan, params = prepare(client, workspace, [{'action': 'remove', 'inventoryId': next(v for v in items if v['projectId'] == new['projectId'])['inventoryId']}])
    client.close()
    helper.kill()  # Simulate process exit while a durable review plan is pending.
    helper = Helper(helper.profile, 'live-restarted')
    client = helper.connect()
    assert client.call('operations.get', {'operationId': op})['state'] == 'prepared'
    assert client.call('plans.get', {'operationId': op})['planDigest'] == plan['planDigest']
    assert client.call('plans.prepare', params)['replayed']
    apply(client, op, plan)
    passed('prepared operation restart and durable result reconnect', operationId=op)
    items = client.call('mods.list', {'physicalId': workspace['physicalId']})['items']
    op, plan, _ = prepare(client, workspace, [{'action': 'remove', 'inventoryId': v['inventoryId']} for v in items])
    apply(client, op, plan)
    assert client.call('mods.list', {'physicalId': workspace['physicalId']})['total'] == 0
    params = {'idempotencyKey': uuid.uuid4().hex, 'physicalId': workspace['physicalId'],
              'instanceRevision': client.call('mods.list', {'physicalId': workspace['physicalId']})['instanceRevision'],
              'changes': [{'action': 'install', 'projectId': new['projectId'], 'versionId': new['versionId']}]}
    cancel = submit(client, 'plans.prepare', params)
    assert client.call('operations.cancel', {'operationId': cancel})['cancellationRequested']
    client.wait(cancel, ('cancelled',))
    assert client.call('mods.list', {'physicalId': workspace['physicalId']})['total'] == 0
    passed('real mod removal, unknown file explicit removal and cancellation')
    helper.shutdown(client)


def main():
    helper = Helper(RUN / 'primary', 'primary')
    client = Client(helper.descriptor)
    client.call('core.info', error='HANDSHAKE_REQUIRED')
    hello = client.hello()
    assert hello['capabilities']['transactions.apply'] and not hello['capabilities']['inbox.begin']
    client.call('core.info', {'path': 'C:/forbidden'}, error='INVALID_REQUEST')
    client.call('inbox.begin', error='UNSUPPORTED_CAPABILITY')
    future = Client(helper.descriptor)
    future.call('hello', {'versions': [{'major': 3, 'minor': 0}, PROTOCOL], 'client': {'name': 'future-fixture', 'version': '1'}})
    future.close()
    incompatible = Client(helper.descriptor)
    incompatible.call('hello', {'versions': [{'major': 1, 'minor': 0}], 'client': {'name': 'v1-fixture', 'version': '1'}}, error='UNSUPPORTED_PROTOCOL', protocol={'major': 1, 'minor': 0})
    incompatible.close()
    info = client.call('core.info')
    assert Path(info['dataRoot']) == helper.profile and info['state'] == 'ready'
    modules = verify_modules(helper)
    second_log = (RUN / 'duplicate-helper.log').open('wb')
    duplicate = subprocess.run([str(args.helper.resolve()), '--profile', str(helper.profile)], cwd=RUN, env=ENV,
                               stdout=second_log, stderr=subprocess.STDOUT, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
    second_log.close()
    assert duplicate.returncode != 0
    passed('packaged helper without Launcher paths, peer identity, strict protocol and profile exclusion', qtModules=modules)
    helper.shutdown(client)
    if args.fixture_executable:
        profile = RUN / 'crash-recovery'
        # Fixture exe uses the same kernel and native serializers, then crashes mid-replacement.
        env = dict(os.environ)
        fixture = subprocess.run([str(args.fixture_executable.resolve()), '--crash-fixture', str(profile)],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, timeout=240, creationflags=subprocess.CREATE_NO_WINDOW)
        assert fixture.returncode == 86, (fixture.returncode, fixture.stderr.decode(errors='replace'))
        record = json.loads(fixture.stdout.decode().strip().splitlines()[-1])
        live_metadata = next((profile / 'workspaces' / record['workspace']['workspaceId'] / '.minecraft/mods/.index').glob('*.pw.toml'))
        locked = K.CreateFileW(str(live_metadata), 0x80000000, 1, None, 3, 0, None)
        assert locked not in (None, C.c_void_p(-1).value), C.get_last_error()
        try:
            helper = Helper(profile, 'recovery-startup')
            client = helper.connect()
            blocked = client.call('operations.get', {'operationId': record['operationId']})
            assert blocked['state'] == 'recovery_required', blocked
            assert client.call('core.info')['state'] == 'recovery_required'
            assert client.call('recovery.list')['operations']
            client.call('core.shutdown', error='BUSY')
            assert not client.call('operations.cancel', {'operationId': record['operationId']})['cancellable']
        finally:
            K.CloseHandle(locked)
        recovery = {'idempotencyKey': uuid.uuid4().hex, 'operationId': record['operationId'], 'action': 'rollback'}
        submit(client, 'recovery.run', recovery)
        final = client.wait(record['operationId'], ('rolled_back',), allow_recovery=True)
        assert client.call('recovery.run', recovery)['replayed']
        assert final['state'] == 'rolled_back' and final['terminal'], final
        assert client.call('mods.list', {'physicalId': record['workspace']['physicalId']})['total'] == 0
        assert client.call('recovery.list')['operations'] == []
        assert final['result']['instanceRevision'] == record['workspace']['instanceRevision']
        passed('packaged helper recovered a 301-mod interrupted native Apply to the exact baseline', operationId=record['operationId'], state=final['state'])
        helper.shutdown(client)
    if args.live:
        live_workflow(Helper(RUN / 'live', 'live'))
    idle = Helper(RUN / 'idle', 'idle')
    client = idle.connect()
    client.close()
    assert idle.process.wait(75) == 0
    passed('helper exited after safe idle timeout without Manager termination')
    print('Evidence: ' + str(RUN), flush=True)


try:
    main()
finally:
    for process in PROCESSES:
        process.kill()
