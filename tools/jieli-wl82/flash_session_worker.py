"""Fixed operations for a protected, session-only FM-1 broker. Not a USB writer.

Run by flash-session-server.ps1 using the existing Python with -I -S. Requests
contain image bytes, never executable code, paths, shell arguments or commands.
The reviewed deployment orchestrator and jluboottool.py still do all writes.
"""
import base64
import hashlib
import importlib
import json
from pathlib import Path
import re
import runpy
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
SIZE = 0x100000
APP_START = 0x4120
APP_END = APP_START + 584956  # Fixed V14 application allocation, not this build's size.
HELPER_SHA = 'd41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376'
PS = r'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe'
CLI = ROOT / 'tools/jl-uboot-tool/jluboottool.py'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(test, message):
    if not test:
        raise ValueError(message)


def schema(request):
    require(type(request) is dict, 'Object required')
    op = request.get('op')
    require(op in ('status', 'environment', 'plan', 'flash', 'retry_flash', 'recover_flash', 'reset', 'observe', 'serial_status', 'enter_uboot', 'read_firmware'), 'Unknown operation')
    if op in ('plan', 'flash', 'retry_flash', 'recover_flash'):
        fields = {'op', 'image', 'sha256', 'baseline_sha256'}
    elif op == 'read_firmware':
        fields = {'op', 'loader_state'}
    else:
        fields = {'op'}
    require(set(request) == fields, 'Unexpected or missing request fields')
    if op == 'read_firmware':
        require(type(request['loader_state']) is str and request['loader_state'] in ('cold', 'reuse'),
                'Explicit cold or reuse loader state required')
    return op


def candidate(request, baseline):
    schema(request)
    require(len(baseline) == SIZE and request['baseline_sha256'] == sha(baseline), 'Stale baseline')
    require(type(request['image']) is str and len(request['image']) <= 1398104, 'Image size invalid')
    image = base64.b64decode(request['image'], validate=True)
    require(len(image) == SIZE and sha(image) == request['sha256'], 'Candidate hash/length mismatch')
    changed = [a for a in range(0, SIZE, 4096) if image[a:a+4096] != baseline[a:a+4096]]
    require(changed and changed[0] == 0x4000, 'Application directory change required')
    # No per-build sector cutoff. Protect every byte outside the stock app
    # allocation and its two CRC/size directory records, including the config
    # bytes sharing the app's final erase sector.
    for start, end in ((0, 0x4000), (0x4040, APP_START), (APP_END, SIZE)):
        require(image[start:end] == baseline[start:end],
                'Candidate changes boot/config/reserved bytes outside the stock application; no device I/O')
    return image, changed[1:] + [0x4000]


def recovery_receipt(baseline, image, sectors, receipt):
    # Keep the original scope even when NOR already contains candidate bytes.
    # An interrupted read must never become the verified baseline.
    _, expected = candidate(dict(op='plan', image=base64.b64encode(image).decode(),
                                 sha256=sha(image), baseline_sha256=sha(baseline)), baseline)
    require(sectors == expected, 'Recovery sector scope differs from the failed candidate')
    require(receipt.get('status') == 'failed' and
            receipt.get('baseline_sha256') == sha(baseline) and
            receipt.get('candidate_sha256') == sha(image), 'Recovery receipt identity/status mismatch')
    verified = receipt.get('verified_sectors')
    require(type(verified) is list and len(verified) <= len(sectors) and
            verified == [hex(a) for a in sectors[:len(verified)]],
            'Recovery verified sectors are not the original ordered prefix')
    current = sectors[len(verified)] if len(verified) < len(sectors) else sectors[-1]
    require(receipt.get('current_sector') == hex(current), 'Recovery current sector is not next')
    return len(verified), current


def verify_recovery_read(baseline, image, sectors, receipt, current):
    prefix, uncertain = recovery_receipt(baseline, image, sectors, receipt)
    require(len(current) == SIZE, 'Recovery read is not the complete flash array')
    for start, end in ((0, 0x4000), (0x4040, APP_START), (APP_END, SIZE)):
        require(current[start:end] == baseline[start:end],
                'Recovery device boot/config/reserved bytes differ from the verified baseline')
    if prefix == len(sectors):
        require(current == image, 'Recovery completed-prefix read differs from the candidate')
    else:
        verified = set(sectors[:prefix])
        for address in range(0, SIZE, 4096):
            actual = current[address:address + 4096]
            if address in verified:
                require(actual == image[address:address + 4096],
                        'Recovery verified sector differs from the candidate')
            elif address != uncertain:
                require(actual == baseline[address:address + 4096],
                        'Recovery untouched sector differs from the baseline')
            # Only the next unverified erase sector may be partly programmed;
            # protected bytes sharing that sector were checked above.
    return dict(current_sha256=sha(current), verified_prefix_sectors=prefix,
                uncertain_sector=hex(uncertain), complete_flash_read=True,
                protected_bytes_match=True)


def resume_failed_session(root):
    """Offline migration of a legacy failed flash; never clear its latch.

    The launcher hashes/copies these evidence files into the new protected
    snapshot. No device reads, executable inputs, or automatic retry here.
    """
    root = Path(root).resolve()
    def inside(path):
        path = path.resolve()
        require(path.is_relative_to(root), 'Recovery evidence outside source session')
        return path
    state_path = root / 'state.json'
    state = json.loads(state_path.read_text(encoding='utf-8-sig'))
    require(state.get('blocked') is True and state.get('failed_operation') == 'flash'
            and not state.get('reset_pending'), 'Recovery requires a failed, unverified flash')
    base_path = inside(root / state['baseline_file'])
    base = base_path.read_bytes()
    require(len(base) == SIZE and sha(base) == state['baseline_sha256'], 'Recovery baseline damaged')
    attempt = state.get('flash_attempt')
    run = inside(root / (attempt['run'] if attempt else state['last_run']))
    receipt = json.loads((run / 'deployment.json').read_text(encoding='utf-8-sig'))
    require(receipt.get('status') == 'failed' and receipt['baseline_sha256'] == sha(base),
            'Recovery requires a completed failure receipt, not an in-progress operation')
    matches = []
    for path in sorted((root / 'ipc').glob('*.json')):
        req = json.loads(path.read_text(encoding='utf-8-sig'))
        if req.get('op') in ('flash', 'retry_flash', 'recover_flash') and req.get('sha256') == receipt['candidate_sha256']:
            image, sectors = candidate(req, base)
            matches.append((path, image, sectors))
    require(matches, 'Failed candidate request missing from protected history')
    request_path, image, sectors = matches[-1]
    verified = receipt.get('verified_sectors', [])
    require(verified == [hex(a) for a in sectors[:len(verified)]]
            and len(verified) <= len(sectors)
            and receipt.get('current_sector') in [hex(a) for a in sectors],
            'Failed receipt does not match candidate write scope')
    target = attempt['target'] if attempt else state.get('target')
    require(isinstance(target, dict) and target.get('PNPDeviceID') and target.get('DeviceID'),
            'Failed target identity missing')
    if attempt:
        require(attempt['status'] == 'failed' and attempt['sha256'] == sha(image)
                and attempt['baseline_sha256'] == sha(base) and attempt['sectors'] == sectors,
                'Recovery attempt metadata mismatch')
    files = [(state_path, 'recovery/source-state.json'),
             (request_path, 'recovery/request.json')]
    files.extend((inside(p), 'recovery/failed-run/' + p.name) for p in run.iterdir() if p.is_file())
    return {'baseline': str(base_path), 'baseline_sha256': sha(base),
            'files': [{'source': str(p), 'relative': rel} for p, rel in files],
            'initial_state': dict(baseline_file='baseline.bin', baseline_sha256=sha(base),
                blocked=True, reset_pending=False, needs_observation=False, operation='idle',
                failed_operation='flash', error=state.get('error'), target=target,
                migrated_from=str(root),
                flash_attempt=dict(status='failed', run='recovery/failed-run',
                    sha256=sha(image), baseline_sha256=sha(base), sectors=sectors, target=target))}


def ps_json(script):
    # Only internal constant scripts, never strings from the IPC request.
    result = subprocess.run([PS, '-NoProfile', '-NonInteractive', '-Command', script],
                            capture_output=True, text=True, check=True, timeout=15)
    return json.loads(result.stdout) if result.stdout.strip() else []


def disk():
    items = ps_json("@(Get-CimInstance Win32_DiskDrive | Where-Object {$_.Model -eq 'WL82 UBOOT1.00 USB Device'} | Select-Object DeviceID,PNPDeviceID) | ConvertTo-Json -Compress")
    if isinstance(items, dict):
        items = [items]
    require(len(items) == 1, 'Exactly one FM-1 UBOOT disk required')
    item = items[0]
    require(re.fullmatch(r'\\\\\.\\PHYSICALDRIVE[0-9]+', item['DeviceID']) is not None,
            'Unexpected disk path')
    require(item['PNPDeviceID'].startswith('USBSTOR\\DISK&VEN_WL82&PROD_UBOOT1.00&REV_1.00\\'),
            'Unexpected PNP identity')
    return item


def trusted_local(root=ROOT):
    config = root / 'session.json'
    if not config.exists():
        return False
    value = json.loads(config.read_text(encoding='utf-8-sig')).get('trusted_local', False)
    require(type(value) is bool, 'Invalid protected trust-mode setting')
    return value


def python_flags():
    return ['-u'] if trusted_local() else ['-I', '-S', '-u']


def environment_info():
    import ctypes
    packages = {name: importlib.import_module(name).__file__
                for name in ('yaml', 'tqdm', 'crcmod', 'colorama', 'serial')}
    return {'trusted_local': trusted_local(), 'python': sys.executable,
            'isolated': bool(sys.flags.isolated), 'no_site': bool(sys.flags.no_site),
            'elevated': bool(ctypes.windll.shell32.IsUserAnAdmin()) if sys.platform == 'win32' else False,
            'packages': packages, 'device_io': False}


def execute_cli(device, commands, log, mode='write'):
    flags = {'write': ['--reuse-loader', '--fm1-bench', '--fm1-nes-cdc'],
             'load': ['--fm1-load'], 'reset': ['--reuse-loader', '--fm1-reset']}[mode]
    args = [sys.executable, *python_flags(), str(Path(__file__).resolve()), '_cli',
            '--device', device, '--chip', 'wl82', '--io-size', '256', *flags, *commands]
    with log.open('x', encoding='utf-8') as stream:
        subprocess.run(args, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT,
                       check=True, timeout=60)
    # The child writes raw bytes to the logfile descriptor. Reset output may use
    # a Windows code page; preserve those bytes and tolerate decoding only here.
    text = log.read_text(encoding='utf-8', errors='replace' if mode == 'reset' else 'strict')
    require('Key mismatch' not in text and 'ERROR' not in text, 'CLI reported an error')
    if mode == 'load':
        require('The Loader has been successfully installed.' in text and '0x856014' in text,
                'RAM helper not confirmed')


def serial_port():
    from serial.tools import list_ports
    ports = [p.device for p in list_ports.comports() if p.vid == 0x3654 and p.pid == 0x5155]
    require(len(ports) == 1, 'Exactly one FM-1 CDC port required')
    return ports[0]


def capture(port, seconds):
    deadline = time.monotonic() + seconds
    output = bytearray()
    while time.monotonic() < deadline:
        output.extend(port.read(min(max(port.in_waiting, 1), 4096)))
        require(len(output) <= 512000, 'Serial capture exceeded bound')
    return output.decode('ascii', errors='replace')


def mod_status_progress(transcript):
    required = {'ready','running','dirty','export','order','row','ch','sample',
                'lcd','keys','audio','frames','underruns'}
    counters = []
    for line in transcript.splitlines():
        if not line.startswith('MOD status '):
            continue
        values = dict(re.findall(r'\b([a-z]+)=(-?\d+)\b', line))
        optional = {'mix','edit','ui','strips'}
        require(required <= set(values) and set(values) <= required | optional,
                'Incomplete MOD status')
        values = {key:int(value) for key,value in values.items()}
        require(values['ready'] == 1 and values['running'] == 0 and
                values['dirty'] == 0 and values['export'] == 0,
                'MOD editor boot state unexpected')
        require(not any(values[key] for key in ('lcd','keys','audio')),
                'MOD editor peripheral error reported')
        counters.append(values)
    require(len(counters) >= 2 and counters[-1]['frames'] > counters[0]['frames'],
            'MOD audio clock progress not confirmed')
    return counters[0], counters[-1]


def doom_status_values(transcript):
    required = {'stage','fault','frames','stopped','lcd_stage','lcd_error',
                'key_error','sys_hz','lsb_hz'}
    counters = []
    for line in transcript.splitlines(keepends=True):
        if not line.startswith('DOOM STATUS ') or not line.endswith('\n'):
            continue
        # The diagnostic message is trailing free text, not status fields.
        prefix = line.split(' error=', 1)[0]
        fields = re.findall(r'(?<!\S)([a-z_]+)=(-?\d+)(?!\S)', prefix)
        require(len(fields) == len(required) and {key for key,value in fields} == required,
                'Incomplete Doom status')
        counters.append({key:int(value) for key,value in fields})
    return counters


def doom_status_progress(transcript):
    counters = doom_status_values(transcript)
    require(counters and not any(values[key] for values in counters
                                for key in ('fault','lcd_error')),
            'Doom firmware/LCD error reported or status missing')
    require(counters[-1]['stage'] == 4 and counters[-1]['stopped'] == 0,
            'Doom renderer not running')
    active = [values for values in counters if values['stage'] == 4 and not values['stopped']]
    require(len(active) >= 2 and active[-1]['frames'] > active[0]['frames'],
            'Doom frame progress not confirmed')
    return active[0], active[-1], any(values['key_error'] for values in counters)


def live_status_samples(transcript, profile):
    """Parse diagnostics without imposing post-flash boot/idle predicates."""
    rules = {
        'DOOM-FM1/1': ('DOOM STATUS ', {'stage', 'fault', 'frames', 'stopped', 'lcd_stage',
                                     'lcd_error', 'key_error', 'sys_hz', 'lsb_hz'}),
        'MOD-EDITOR/1': ('MOD status ', {'ready', 'running', 'dirty', 'export', 'order', 'row',
                                      'ch', 'sample', 'lcd', 'keys', 'audio', 'frames', 'underruns'}),
        'MDX-KARAOKE/1': ('MDX running=', {'running', 'pending', 'frames', 'underruns',
                                        'error', 'lcd', 'keys', 'audio'}),
        'NES': ('TEST STATUS ', {'ready', 'busy', 'mode', 'dropped', 'frames', 'irqs', 'fault'}),
    }
    prefix, required = rules[profile]
    samples = []
    for line in transcript.splitlines(keepends=True):
        if not line.startswith(prefix) or not line.endswith('\n'):
            continue
        line = line.split(' error=', 1)[0] if profile == 'DOOM-FM1/1' else line
        fields = re.findall(r'(?<!\S)([a-z_]+)=(-?\d+)(?!\S)', line)
        values = {key: int(value) for key, value in fields}
        require(required <= set(values) and len(fields) == len(values), 'Incomplete live status')
        samples.append(values)
    require(samples, 'Live firmware status missing')
    return samples


def serial_action(op, run):
    import serial
    # Enumeration may lag a successful reset. No flash/reset retries here.
    deadline = time.monotonic() + (15 if op == 'observe' else 1)
    while True:
        try:
            name = serial_port()
            break
        except ValueError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(.5)
    transcript = ''
    try:
        with serial.Serial(port=None, baudrate=115200, timeout=.1, write_timeout=1) as port:
            port.port = name
            port.dtr = True
            port.rts = False
            port.open()
            transcript += capture(port, 1)
            for _ in range(3):
                port.write(b'HELLO\n'); port.flush()
                reply = capture(port, 1)
                transcript += reply
                doom_profile = 'FM1DIAG/1 DOOM-FM1/1 UBOOT=SERIAL COMMIT=BLOCKED' in reply.splitlines()
                if 'FM1DIAG/1' in reply and (re.search(r'\bNES=(?:SMB1|INES)\b',reply) or
                                           'MDX-KARAOKE/1 RAM-UPLOAD UBOOT=SERIAL COMMIT=BLOCKED' in reply or
                                           'MOD-EDITOR/1 RAM-TRANSFER UBOOT=SERIAL COMMIT=BLOCKED' in reply or
                                           doom_profile):
                    break
            else:
                raise ValueError('FM-1 serial HELLO not confirmed')
            if op == 'serial_status':
                require(len(transcript) <= 65536, 'Live serial transcript exceeded bound')
                if doom_profile:
                    profile, command = 'DOOM-FM1/1', 'DOOM STATUS'
                elif 'MOD-EDITOR/1 RAM-TRANSFER' in reply:
                    profile, command = 'MOD-EDITOR/1', 'MOD STATUS'
                elif 'MDX-KARAOKE/1' in reply:
                    profile, command = 'MDX-KARAOKE/1', 'MDX STATUS'
                else:
                    profile, command = 'NES', 'TEST STATUS'
                observation = ''
                for _ in range(4):
                    port.write((command + '\n').encode('ascii')); port.flush()
                    status = capture(port, 1)
                    observation += status; transcript += status
                    require(len(transcript) <= 65536, 'Live serial transcript exceeded bound')
                # Live playback, edits and reported faults are diagnostic data.
                # This operation sends no STOP/UBOOT and grants no boot permit.
                return {'serial_hello_verified': True, 'profile': profile, 'port': name,
                        'status_command': command,
                        'samples': live_status_samples(observation, profile),
                        'transcript': transcript, 'diagnostic_only': True, 'boot_verified': False}
            if op == 'observe':
                if doom_profile:
                    observation = ''
                    for _ in range(4):
                        port.write(b'DOOM STATUS\n'); port.flush()
                        status = capture(port, 1)
                        observation += status; transcript += status
                    first,last,key_error = doom_status_progress(observation)
                    return {'serial_hello_verified': True, 'profile': 'DOOM-FM1/1', 'port': name,
                            'frames_first': first['frames'], 'frames_last': last['frames'],
                            'stage': last['stage'], 'fault': last['fault'],
                            'lcd_stage': last['lcd_stage'], 'lcd_error': last['lcd_error'],
                            'key_error_first': first['key_error'], 'key_error_last': last['key_error'],
                            'key_error_reported': key_error,
                            'sys_hz': last['sys_hz'], 'lsb_hz': last['lsb_hz'],
                            'physical_display_keys_verified': False,
                            'note': 'Serial renderer/LCD counters only; inspect physical screen and keys.'}
                if 'MOD-EDITOR/1 RAM-TRANSFER' in reply:
                    for _ in range(4):
                        port.write(b'MOD STATUS\n'); port.flush()
                        transcript += capture(port, 1)
                    require('FAULT' not in transcript, 'Firmware failure reported; inspect serial log')
                    first,last = mod_status_progress(transcript)
                    return {'serial_hello_verified': True, 'profile': 'MOD-EDITOR/1', 'port': name,
                            'frames_first': first['frames'], 'frames_last': last['frames'],
                            'underruns_first': first['underruns'], 'underruns_last': last['underruns'],
                            'physical_display_audio_verified': False,
                            'note': 'Serial clock/peripheral counters only; inspect physical screen/audio.'}
                if 'MDX-KARAOKE/1' in reply:
                    for _ in range(6):
                        port.write(b'MDX STATUS\n'); port.flush()
                        transcript += capture(port, 2)
                    require('FAULT' not in transcript, 'Firmware failure reported; inspect serial log')
                    lines = [line for line in transcript.splitlines() if line.startswith('MDX running=')]
                    counters = []
                    for line in lines:
                        values = dict(re.findall(r'\b(running|pending|frames|underruns|error|lcd|keys|audio)=(-?\d+)\b', line))
                        require(set(values) == {'running','pending','frames','underruns','error','lcd','keys','audio'},
                                'Incomplete MDX status')
                        values = {k: int(v) for k,v in values.items()}
                        require(values['running'] == 1 and not values['pending'], 'MDX playback not running')
                        require(not any(values[k] for k in ('error','lcd','keys','audio')),
                                'MDX firmware/peripheral error reported')
                        counters.append(values)
                    require(len(counters) >= 2 and counters[-1]['frames'] > counters[0]['frames'],
                            'MDX audio frame progress not confirmed')
                    return {'serial_hello_verified': True, 'profile': 'MDX-KARAOKE/1', 'port': name,
                            'frames_first': counters[0]['frames'], 'frames_last': counters[-1]['frames'],
                            'underruns_first': counters[0]['underruns'], 'underruns_last': counters[-1]['underruns'],
                            'physical_display_audio_verified': False,
                            'note': 'Serial playback/peripheral counters only; inspect physical screen/audio.'}
                port.write(b'TEST STATUS\n'); port.flush()
                transcript += capture(port, 12)
                require('NES FAILURE' not in transcript and 'FAULT' not in transcript,
                        'Firmware failure reported; inspect serial log')
                frames = [int(n) for n in re.findall(r'NES frames=(\d+)', transcript)]
                faults = [int(n) for n in re.findall(r'\bfault=(-?\d+)', transcript)]
                require(len(frames) >= 2 and frames[-1] > frames[0], 'NES frame progress not confirmed')
                require(faults and not any(faults), 'Firmware fault reported or fault status missing')
                return {'serial_hello_verified': True, 'port': name,
                        'frames_first': frames[0], 'frames_last': frames[-1],
                        'physical_display_audio_verified': False,
                        'note': 'Serial capture only; inspect counters and physical screen/audio.'}
            if doom_profile:
                port.write(b'DOOM STOP\n'); port.flush()
                status = capture(port, 2); transcript += status
                for _ in range(6):
                    port.write(b'DOOM STATUS\n'); port.flush()
                    reply = capture(port, .7); status += reply; transcript += reply
                    counters = doom_status_values(status)
                    if counters and counters[-1]['stopped'] == 1:
                        break
                else:
                    raise ValueError('Doom renderer not confirmed stopped')
            elif 'MOD-EDITOR/1 RAM-TRANSFER' in reply:
                port.write(b'MOD STOP\n'); port.flush()
                transcript += capture(port, 2)
                port.write(b'MOD STATUS\n'); port.flush()
                status = capture(port, 2); transcript += status
                require(re.search(r'MOD status ready=1 running=0\b', status) is not None,
                        'MOD player not confirmed stopped')
            elif 'MDX-KARAOKE/1' in reply:
                port.write(b'MDX STOP\n'); port.flush()
                transcript += capture(port, 2)
                port.write(b'MDX STATUS\n'); port.flush()
                status = capture(port, 2); transcript += status
                require(re.search(r'MDX running=0 pending=0\b', status) is not None,
                        'MDX player not confirmed stopped')
                # UBOOT itself requests full peripheral teardown; it must arm
                # only after that teardown completes, using the same handshake.
            else:
                port.write(b'TEST STOP\n'); port.flush()
                transcript += capture(port, 2)
                port.write(b'TEST STATUS\n'); port.flush()
                status = capture(port, 2); transcript += status
                require(re.search(r'busy[=:]0\b', status) is not None, 'Peripheral worker not confirmed idle')
            for _ in range(3):
                port.write(b'UBOOT\n'); port.flush()
                reply = capture(port, .7); transcript += reply
                if 'OK UBOOT ARMED CONFIRM-WITHIN-5000MS' in reply:
                    port.write(b'UBOOT CONFIRM\n'); port.flush()
                    break
                require('ERR UBOOT_RETRY' in reply, 'UBOOT arm refused')
                transcript += capture(port, 2)
            else:
                raise ValueError('UBOOT arm timeout')
        time.sleep(1)
        return {'uboot': disk()}
    finally:
        (run / 'serial.log').write_text(transcript, encoding='utf-8')


class Session:
    def __init__(self, root=ROOT):
        self.root = root
        self.state_path = root / 'state.json'
        self.state = json.loads(self.state_path.read_text(encoding='utf-8-sig'))

    def save(self):
        temp = self.state_path.with_suffix('.new')
        temp.write_text(json.dumps(self.state, indent=2), encoding='utf-8')
        temp.replace(self.state_path)

    def handle(self, request):
        op = schema(request)
        if op == 'status':
            return dict(self.state)
        if op == 'environment':
            return environment_info()
        if op == 'serial_status':
            # The pipe serializes callers. A completed failure may leave its
            # operation name in state; permit diagnosis without clearing it.
            require(self.state.get('operation', 'idle') == 'idle' or
                    (self.state.get('blocked') and self.state.get('failed_operation') and
                     self.state.get('error')), 'Session operation pending; inspect state before serial status')
            run = self.root / 'runs' / uuid.uuid4().hex
            run.mkdir(parents=True)
            result = serial_action(op, run)
            return {'result': result, 'log_directory': str(run)}
        if op == 'read_firmware':
            require(not self.state.get('reset_pending') and not self.state.get('needs_observation'),
                    'Verified image still requires reset/observation before firmware read')
            require(request['loader_state'] != 'cold' or not self.state.get('loader_running'),
                    'Known RAM helper is running; use reuse or establish a fresh cold UBOOT through the local procedure')
            base_path = (self.root / self.state['baseline_file']).resolve()
            require(base_path.is_relative_to(self.root.resolve()), 'Baseline outside protected session')
            base = base_path.read_bytes()
            require(len(base) == SIZE and sha(base) == self.state['baseline_sha256'], 'Stored baseline damaged')
        if op in ('plan', 'flash', 'retry_flash', 'recover_flash'):
            base = (self.root / self.state['baseline_file']).read_bytes()
            require(sha(base) == self.state['baseline_sha256'], 'Stored baseline damaged')
            image, sectors = candidate(request, base)
            if op == 'plan':
                return {'sha256': sha(image), 'sectors': [hex(x) for x in sectors],
                        'device_io': False, 'blocked': self.state['blocked']}
        retry = self.state.get('flash_attempt') if op in ('retry_flash', 'recover_flash') else None
        if op in ('retry_flash', 'recover_flash'):
            require(self.state['blocked'] and self.state.get('failed_operation') == 'flash'
                    and retry and retry.get('status') == 'failed' and retry.get('target'),
                    'Explicit retry requires a completed failed flash with a recorded target')
            require(retry['sha256'] == sha(image) and retry['baseline_sha256'] == sha(base)
                    and retry['sectors'] == sectors, 'Retry must use the exact failed candidate and original sector scope')
        recovery = None
        if op == 'recover_flash':
            source_run = (self.root / retry['run']).resolve()
            require(source_run.is_relative_to(self.root.resolve()), 'Recovery failure evidence outside session')
            recovery = json.loads((source_run / 'deployment.json').read_text(encoding='utf-8-sig'))
            recovery_receipt(base, image, sectors, recovery)
        require(not self.state['blocked'] or op in ('observe', 'retry_flash', 'recover_flash'),
                'Session latched after failure; inspect logs and use explicit retry_flash for the same failed image.')
        if op == 'reset':
            require(self.state.get('reset_pending'), 'Reset requires a successful verified flash in this session')
        if op in ('flash', 'retry_flash', 'recover_flash'):
            require(not self.state.get('reset_pending') and not self.state.get('needs_observation'),
                    'Previous verified image has not completed reset/observation')
            require(not self.state.get('loader_running'),
                    'Firmware read left a RAM helper running; establish a fresh cold UBOOT through the local power-cycle/CDC procedure before flash')
        run = self.root / 'runs' / uuid.uuid4().hex
        run.mkdir(parents=True)
        self.state['last_run'] = str(run)
        self.state['operation'] = op
        # Persist failure latch BEFORE device I/O; process termination leaves it closed.
        was_blocked = self.state['blocked']
        previous_operation = self.state.get('failed_operation')
        self.state['blocked'] = True
        self.save()
        deployment_started = False
        try:
            if op in ('flash', 'retry_flash', 'recover_flash'):
                target = disk()
                # A replug can change PHYSICALDRIVE numbering; require the same
                # recorded PNP identity, then pin both values for this attempt.
                if retry and op == 'retry_flash':
                    require(target['PNPDeviceID'] == retry['target']['PNPDeviceID'], 'Retry target identity changed')
                attempt = dict(status='pending', run=str(run.relative_to(self.root)),
                    sha256=sha(image), baseline_sha256=sha(base), sectors=sectors, target=target,
                    retry_of=retry['run'] if retry else None)
                (run / 'attempt.json').write_text(json.dumps(attempt, indent=2), encoding='utf-8')
                self.state.update(flash_attempt=attempt, target=target)
                self.state.pop('reset_resolved_by_observation', None)
                self.save()
                helper = ROOT / 'tools/jl-uboot-tool/data/loaderblobs/usb/wl82loader.bin'
                require(sha(helper.read_bytes()) == HELPER_SHA, 'Helper hash mismatch')
                execute_cli(target['DeviceID'], ['help'], run / 'helper.log', 'load')
                require(disk() == target, 'USB identity changed after helper upload')
                if op == 'recover_flash':
                    current_path = run / 'current.bin'
                    execute_cli(target['DeviceID'], [f'read 0 {SIZE} {current_path}'], run / 'current.log')
                    require(disk() == target, 'USB identity changed during recovery read')
                    proof = verify_recovery_read(base, image, sectors, recovery, current_path.read_bytes())
                    proof.update(previous_target=retry['target'], target=target,
                                 source_failed_run=retry['run'])
                    (run / 'recovery-read.json').write_text(json.dumps(proof, indent=2), encoding='utf-8')
                    attempt['recovery_read'] = proof
                    (run / 'attempt.json').write_text(json.dumps(attempt, indent=2), encoding='utf-8')
                    self.save()
                import deploy_v14_smb1_via_cli as writer
                def call(device, commands, log):
                    require(disk() == target, 'USB identity changed during write/verify')
                    execute_cli(device, commands, log)
                    require(disk() == target, 'USB identity changed after write/verify')
                # Recovery validates a fresh dump against the interrupted write.
                # All operations retain the original baseline and sector scope;
                # erase/readback, directory-last and final verification are unchanged.
                deployment_started = True
                result = writer.deploy(target['DeviceID'], run, base, image, call=call,
                    sectors=sectors, verified_preflight=self.root / self.state['baseline_file'], nes_cdc=True)
                require(result['status'] == 'written_and_readback_verified' and result['full_readback_count'] == 1,
                        'Verification incomplete')
                require((run / 'final-1.bin').read_bytes() == image, 'Final read differs')
                self.state.update(baseline_file=str((run / 'final-1.bin').relative_to(self.root)),
                                  baseline_sha256=sha(image), reset_pending=True, needs_observation=True, target=target)
                attempt['status'] = 'verified'
                (run / 'attempt.json').write_text(json.dumps(attempt, indent=2), encoding='utf-8')
                self.state.pop('failed_operation', None)
                self.state.pop('error', None)
            elif op == 'read_firmware':
                target = disk()
                recorded = self.state.get('target')
                if recorded:
                    require(target['PNPDeviceID'] == recorded['PNPDeviceID'], 'Firmware read target identity changed')
                # Mark ownership before any helper/read command. A partial
                # upload or lost response cannot authorize a later re-upload.
                self.state['loader_running'] = True
                self.save()
                # Inquiry text cannot distinguish cold ROM from a running RAM
                # helper. The caller must establish cold/reuse from history.
                # Helper initialization/online can change NOR protection bits,
                # including in reuse mode; no array erase/program is requested.
                if request['loader_state'] == 'cold':
                    helper = ROOT / 'tools/jl-uboot-tool/data/loaderblobs/usb/wl82loader.bin'
                    require(sha(helper.read_bytes()) == HELPER_SHA, 'Helper hash mismatch')
                    require(disk() == target, 'USB identity changed before helper upload')
                    execute_cli(target['DeviceID'], ['help'], run / 'helper.log', 'load')
                    require(disk() == target, 'USB identity changed after helper upload')
                reads = []
                for number in (1, 2):
                    path = run / f'readback-{number}.bin'
                    require(disk() == target, 'USB identity changed before firmware read')
                    execute_cli(target['DeviceID'], [f'read 0 {SIZE} {path}'], run / f'readback-{number}.log')
                    require(disk() == target, 'USB identity changed after firmware read')
                    data = path.read_bytes()
                    require(len(data) == SIZE, 'Short full firmware read')
                    reads.append(data)
                require(reads[0] == reads[1] and sha(reads[0]) == sha(reads[1]), 'Firmware reads differ; no export finalized')
                exported = run / 'firmware.bin'
                (run / 'readback-1.bin').rename(exported)
                result = {'sha256': sha(reads[0]), 'size': SIZE, 'readback_count': 2,
                          'loader_state': request['loader_state'],
                          'file': str(exported.relative_to(self.root)), 'target': target,
                          'chip_key_queried': False, 'flash_array_program_or_erase_commands_sent': False,
                          'reset_requested': False, 'includes_device_configuration': True,
                          'loader_online_may_change_nor_protection': True}
                (run / 'firmware.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
            elif op == 'reset':
                target = disk()
                require(target == self.state['target'], 'Reset target changed')
                # Consume reset permit first. A transport disconnect may be expected,
                # but is ambiguous; never resend it automatically.
                self.state['reset_pending'] = False
                self.save()
                execute_cli(target['DeviceID'], ['reset 1'], run / 'reset.log', 'reset')
                self.state['loader_running'] = False
                result = {'reset_command_completed': True, 'boot_verified': False}
            else:
                result = serial_action(op, run)
                if op == 'observe':
                    self.state['needs_observation'] = False
                elif op == 'enter_uboot':
                    # The supported CDC teardown/arm/confirm establishes a
                    # newly entered UBOOT session rather than helper reuse.
                    self.state['loader_running'] = False
            self.state['blocked'] = was_blocked if op == 'observe' else False
            # A reset transport disconnect can be resolved by verified CDC and
            # advancing firmware telemetry, but a failed flash can NEVER be cleared this way.
            if op == 'observe' and previous_operation == 'reset' and not self.state.get('reset_pending'):
                self.state['blocked'] = False
                self.state['reset_resolved_by_observation'] = True
            self.state['operation'] = 'idle'
            self.state['last_result'] = result
            self.save()
            return {'result': result, 'log_directory': str(run)}
        except Exception as error:
            if op in ('flash', 'retry_flash', 'recover_flash'):
                self.state['failed_operation'] = 'flash'
                attempt = self.state.get('flash_attempt')
                if attempt and attempt['run'] == str(run.relative_to(self.root)):
                    attempt['status'] = 'failed'
                    (run / 'attempt.json').write_text(json.dumps(attempt, indent=2), encoding='utf-8')
                    if op == 'recover_flash' and not deployment_started:
                        # No NOR write was attempted. Retain the original
                        # failure evidence and this rejected preflight separately.
                        self.state['recovery_preflight_attempt'] = attempt
                        self.state['flash_attempt'] = retry
                        self.state['target'] = retry['target']
                (run / 'failure.json').write_text(json.dumps({'error': f'{type(error).__name__}: {error}',
                    'operation': op, 'retry_of': retry['run'] if retry else None}), encoding='utf-8')
            # Preserve the original flash failure across later observation errors.
            elif not was_blocked:
                self.state['failed_operation'] = op
            self.state['error'] = f'{type(error).__name__}: {error}'
            self.save()
            raise


def main():
    if sys.argv[1:2] == ['_resume_plan']:
        require(len(sys.argv) == 3, 'Source session required')
        print(json.dumps(resume_failed_session(Path(sys.argv[2]))))
        return
    # Trusted-local explicitly retains the account's normal site/environment.
    # Writer code remains the protected snapshot; dependency trust is broader.
    sys.path[:0] = [str(HERE), *([] if trusted_local() else [str(ROOT / 'lib')]), str(CLI.parent)]
    if sys.argv[1:2] == ['_cli']:
        sys.argv = [str(CLI), *sys.argv[2:]]
        runpy.run_path(str(CLI), run_name='__main__')
        return
    request = json.loads(Path(sys.argv[1]).read_text(encoding='utf-8-sig'))
    try:
        result = {'ok': True, 'data': Session().handle(request)}
    except Exception as error:
        result = {'ok': False, 'error': f'{type(error).__name__}: {error}'}
    print(json.dumps(result))


if __name__ == '__main__':
    main()
