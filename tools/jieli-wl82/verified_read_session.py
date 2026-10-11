"""File-only proof for an explicitly selected protected double-read baseline.

The launcher audits Windows ownership/ACLs before calling this module and pins
every returned hash before UAC. This module never opens a device, pipe, or USB
endpoint, and never writes or stops the source session.
"""
import argparse
import ctypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import stat

SIZE = 0x100000
READ_FIELDS = {
    'sha256', 'size', 'readback_count', 'loader_state', 'file', 'target',
    'chip_key_queried', 'flash_array_program_or_erase_commands_sent',
    'reset_requested', 'includes_device_configuration',
    'loader_online_may_change_nor_protection',
}
STATE_FIELDS = {
    'baseline_file', 'baseline_sha256', 'operation', 'blocked', 'reset_pending',
    'loader_running', 'last_run', 'last_result', 'target', 'baseline_target',
    'needs_observation', 'failed_operation', 'error', 'flash_attempt',
    'recovery_preflight_attempt', 'reset_resolved_by_observation',
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def _json(data):
    def unique_pairs(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, 'Duplicate evidence JSON field')
            result[key] = value
        return result
    result = json.loads(data.decode('utf-8-sig'), object_pairs_hook=unique_pairs)
    require(type(result) is dict, 'Evidence JSON object required')
    return result


def _no_reparse(path):
    for current in (path, *path.parents):
        info = current.lstat()
        require(not stat.S_ISLNK(info.st_mode) and
                not (getattr(info, 'st_file_attributes', 0) & 0x400),
                'Reparse evidence path rejected')


def _relative(root, value):
    require(type(value) is str and value, 'Evidence path required')
    path = Path(value)
    if path.is_absolute():
        require(path.is_relative_to(root), 'Evidence outside source session')
        parts = path.relative_to(root).parts
    else:
        # Receipts use Windows separators even in portable offline fixtures.
        parts = tuple(value.replace('\\', '/').split('/'))
    for part in parts:
        require(part and part not in ('.', '..') and
                not part.endswith(('.', ' ')) and
                not re.search(r'[:<>"|?*\x00-\x1f]', part) and
                not re.fullmatch(r'(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\..*)?', part, re.I),
                'Invalid evidence path segment')
    require(parts, 'Evidence file path required')
    result = root.joinpath(*parts)
    _no_reparse(result)
    require(result.resolve().is_relative_to(root), 'Evidence outside source session')
    return result


def _target(value):
    require(type(value) is dict and set(value) == {'DeviceID', 'PNPDeviceID'} and
            all(type(item) is str and item.strip() and len(item) <= 1024
                for item in value.values()), 'Read target identity invalid')
    return value


def _pid_exists(pid):
    """Query process existence only; do not signal or terminate any process."""
    require(os.name == 'nt', 'Production stopped proof requires Windows')
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
    kernel.OpenProcess.restype = ctypes.c_void_p
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel.OpenProcess(0x1000, False, pid)
    if not handle:
        error = ctypes.get_last_error()
        if error == 87:  # ERROR_INVALID_PARAMETER: no such PID.
            return False
        require(error == 5, 'Cannot establish stopped helper PID')
        return True  # Access denied cannot authorize migration.
    try:
        return True
    finally:
        kernel.CloseHandle(handle)


def plan_verified_read_session(root, *, require_stopped=True, pid_exists=None):
    """Return a hash-pinned copy plan; production ACL proof belongs to launcher."""
    root = Path(root).absolute()
    _no_reparse(root)
    require(root.is_dir() and root.resolve() == root, 'Canonical source directory required')
    evidence = {}

    def read(value):
        path = _relative(root, value)
        require(path.is_file(), 'Regular evidence file required')
        data = path.read_bytes()
        evidence[path] = data
        return path, data

    descriptor_path, descriptor_bytes = read('session.json')
    descriptor = _json(descriptor_bytes)
    require(type(descriptor.get('root')) is str and Path(descriptor['root']) == root,
            'Session descriptor root mismatch')
    require(type(descriptor.get('server_pid')) is int and descriptor['server_pid'] > 0 and
            type(descriptor.get('pipe')) is str and descriptor['pipe'] and
            type(descriptor.get('owner_sid')) is str and descriptor['owner_sid'],
            'Session descriptor helper identity missing')
    for mode in ('trusted_local', 'freeze_python_runtime'):
        require(mode not in descriptor or type(descriptor[mode]) is bool,
                'Invalid source runtime mode')
    require(not descriptor.get('trusted_local'), 'Trusted-local read source rejected')

    state_path, state_bytes = read('state.json')
    state = _json(state_bytes)
    require(set(state) <= STATE_FIELDS, 'Unsupported source state field')
    require(state.get('operation') == 'idle' and state.get('blocked') is False and
            state.get('reset_pending') is False and state.get('loader_running') is True,
            'Verified read requires idle, unblocked source with retained loader gate')
    for flag in ('needs_observation', 'reset_resolved_by_observation'):
        require(flag not in state or type(state[flag]) is bool, 'Invalid source gate type')
    require(not any(state.get(key) for key in (
        'needs_observation', 'failed_operation', 'error', 'flash_attempt',
        'recovery_preflight_attempt',
    )), 'Pending failure, reset, or observation evidence rejected')

    run = _relative(root, state.get('last_run'))
    relative_run = run.relative_to(root)
    require(len(relative_run.parts) == 2 and relative_run.parts[0] == 'runs' and
            re.fullmatch('[0-9a-f]{32}', relative_run.parts[1]), 'Canonical read run required')
    receipt_path, receipt_bytes = read(str(relative_run / 'firmware.json'))
    receipt = _json(receipt_bytes)
    require(set(receipt) == READ_FIELDS, 'Unsupported read receipt schema')
    require(json.dumps(state.get('last_result'), sort_keys=True) == json.dumps(receipt, sort_keys=True),
            'Saved result differs from read receipt')
    require(type(receipt['size']) is int and receipt['size'] == SIZE and
            type(receipt['readback_count']) is int and receipt['readback_count'] == 2 and
            receipt['loader_state'] in ('cold', 'reuse') and
            type(receipt['sha256']) is str and re.fullmatch('[0-9a-f]{64}', receipt['sha256']),
            'Complete double-read receipt required')
    for flag, expected in (
        ('chip_key_queried', False), ('flash_array_program_or_erase_commands_sent', False),
        ('reset_requested', False), ('includes_device_configuration', True),
        ('loader_online_may_change_nor_protection', True),
    ):
        require(receipt[flag] is expected, 'Read receipt command/gate proof invalid')
    target = _target(receipt['target'])
    for field in ('target', 'baseline_target'):
        if state.get(field) is not None:
            require(_target(state[field])['PNPDeviceID'] == target['PNPDeviceID'],
                    'Source target identity differs from read receipt')
    image_path, image = read(str(relative_run / 'firmware.bin'))
    require(_relative(root, receipt['file']) == image_path, 'Read receipt image path mismatch')
    second_path, second = read(str(relative_run / 'readback-2.bin'))
    require(len(image) == SIZE and image == second and sha(image) == receipt['sha256'],
            'Double-read images damaged or different')

    old_baseline_path, old_baseline = read(state.get('baseline_file'))
    require(len(old_baseline) == SIZE and sha(old_baseline) == state.get('baseline_sha256'),
            'Original baseline hash/size mismatch')
    original_receipt_name = ('baseline-deployment.json' if old_baseline_path.name == 'baseline.bin'
                             else 'deployment.json')
    old_receipt_path, old_receipt_bytes = read(str(old_baseline_path.parent.relative_to(root) /
                                                 original_receipt_name))
    old_receipt = _json(old_receipt_bytes)
    require(old_receipt.get('status') in ('written_and_twice_readback_verified',
                                        'written_and_readback_verified') and
            old_receipt.get('full_readback_sha256') == state['baseline_sha256'],
            'Original baseline requires verified deployment receipt')
    if old_receipt['status'] == 'written_and_readback_verified':
        require(type(old_receipt.get('full_readback_count')) is int and
                old_receipt['full_readback_count'] == 1, 'Invalid original single-readback receipt')

    ready_path, ready_bytes = read('ready.json')
    ready = _json(ready_bytes)
    require(ready.get('ready') is True and type(ready.get('server_pid')) is int and
            ready['server_pid'] == descriptor['server_pid'], 'Ready helper identity mismatch')
    stopped_proof = False
    if require_stopped:
        stopped_path, stopped_bytes = read('stopped.txt')
        try:
            stopped = datetime.fromisoformat(stopped_bytes.decode('utf-8-sig').strip().replace('Z', '+00:00'))
        except (ValueError, UnicodeError) as error:
            raise ValueError('Invalid stopped timestamp') from error
        require(stopped.tzinfo is not None and stopped.utcoffset().total_seconds() == 0 and
                stopped.timestamp() <= datetime.now(timezone.utc).timestamp() + 5,
                'Invalid stopped UTC timestamp')
        require(all(stopped.timestamp() + 2 >= path.stat().st_mtime for path in evidence),
                'Stopped marker predates session evidence')
        marker_path = root / 'stopped.json'
        if marker_path.exists():
            _, marker_bytes = read('stopped.json')
            marker = _json(marker_bytes)
            require(set(marker) == {'root', 'pipe', 'server_pid', 'explicit_quit', 'stopped_at'} and
                    marker['root'] == descriptor['root'] and marker['pipe'] == descriptor['pipe'] and
                    type(marker['server_pid']) is int and marker['server_pid'] == descriptor['server_pid'] and
                    marker['explicit_quit'] is True and marker['stopped_at'] == stopped_bytes.decode('utf-8-sig').strip(),
                    'Explicit quit marker identity mismatch')
        require(not (pid_exists or _pid_exists)(descriptor['server_pid']),
                'Source helper PID still exists; quit it before migration')
        stopped_proof = True

    # Preserve exact source records and both old/new full images. Never create
    # a deployment receipt for a read, or alter/delete the original session.
    for optional in ('snapshot.json', *[str(relative_run / name) for name in
                                      ('helper.log', 'readback-1.log', 'readback-2.log')]):
        if (root / optional).exists():
            read(optional)
    files = [dict(source=str(path), relative=str(Path('readback-source') / path.relative_to(root)),
                  sha256=sha(data)) for path, data in evidence.items()]
    files.extend([
        dict(source=str(image_path), relative='baseline.bin', sha256=sha(image)),
        dict(source=str(receipt_path), relative='baseline-readback.json', sha256=sha(receipt_bytes)),
    ])
    provenance = dict(source_root=str(root), source_run=str(relative_run),
                      source_baseline_sha256=state['baseline_sha256'],
                      readback_sha256=receipt['sha256'], stopped_proof=stopped_proof)
    initial = dict(baseline_file='baseline.bin', baseline_sha256=receipt['sha256'],
                   operation='idle', blocked=False, reset_pending=False,
                   loader_running=True, target=dict(target), baseline_target=dict(target),
                   baseline_readback=provenance)
    return dict(baseline=str(image_path), baseline_sha256=receipt['sha256'],
                files=files, initial_state=initial, source=provenance)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root')
    parser.add_argument('--prepare-only', action='store_true',
                        help='Validate idle read evidence without requiring the helper stopped')
    args = parser.parse_args()
    print(json.dumps(plan_verified_read_session(args.root, require_stopped=not args.prepare_only)))


if __name__ == '__main__':
    main()
