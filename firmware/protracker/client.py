"""Tethered .MOD load/save for the FM-1 tracker candidate. No flash operations."""
import argparse
from contextlib import closing
import os
from pathlib import Path
import re
import tempfile
import time
import zlib

LIMIT = 256 * 1024
IDENTITY = 'FM1DIAG/1 MOD-EDITOR/1 RAM-TRANSFER UBOOT=SERIAL COMMIT=BLOCKED'


def expected_reply(command, answer):
    if command == 'HELLO':
        return answer == IDENTITY
    if command == 'MOD STATUS':
        return answer.startswith('MOD status ')
    if command == 'MOD KEY':
        return answer.startswith('MOD key ')
    if command == 'MOD VOL':
        return answer.startswith('MOD vol ')
    if command == 'MOD INFO':
        return answer.startswith('MOD INFO ')
    if command.startswith('MOD READ '):
        return answer.startswith('MOD DATA ')
    if command.startswith('MOD DATA '):
        return answer.startswith('OK MOD DATA ')
    if command == 'LIB POLL':
        return answer == 'LIB IDLE' or answer.startswith('LIB PICK ')
    if command.startswith('LIB ADD '):
        return answer == 'OK LIB ADD'
    if command.startswith('LIB DONE '):
        return answer == 'OK LIB DONE'
    if command.startswith('LIB FAIL '):
        return answer == 'OK LIB FAIL'
    replies = {'MOD STOP':'OK MOD QUEUED','MOD PLAY':'OK MOD QUEUED',
               'MOD NEW':'OK MOD QUEUED','MOD END':'OK MOD READY RAM VOLATILE',
               'MOD ABORT':'OK MOD ABORT','LIB RESET':'OK LIB RESET',
               'LIB READY':'OK LIB READY'}
    if command.startswith('MOD BEGIN '):
        return answer == 'OK MOD BEGIN RAM'
    if command.startswith('MOD SAVED '):
        return answer == 'OK MOD SAVED'
    if command in replies:
        return answer == replies[command]
    raise ValueError(f'Unknown client command: {command.split()[0:2]}')


def exchange(port, line):
    port.write((line + '\n').encode('ascii'))
    port.flush()
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        answer = port.readline().decode('ascii', 'strict').strip()
        if not answer or answer.startswith('#'):
            continue
        if answer.startswith('ERR '):
            raise RuntimeError(answer)
        if line == 'HELLO' and answer.startswith('FM1DIAG/1 ') and answer != IDENTITY:
            raise RuntimeError('FM-1 MOD editor identity mismatch')
        if expected_reply(line, answer):
            return answer
    raise TimeoutError(f'No reply to {line.split()[0:2]}')


def identify(port):
    # Opening CDC in the middle of a periodic trace can leave one partial line
    # after the input-buffer reset. The first command on a newly opened CDC
    # endpoint is also occasionally lost, so retry only this idempotent probe.
    for attempt in range(5):
        port.write(b'HELLO\n')
        port.flush()
        deadline = time.monotonic() + 1.5
        while time.monotonic() < deadline:
            answer = port.readline().decode('ascii', 'strict').strip()
            if answer == IDENTITY:
                time.sleep(.1)
                return
            if answer.startswith('FM1DIAG/1 ') and not answer.startswith('#'):
                raise RuntimeError('FM-1 MOD editor identity mismatch')
    raise TimeoutError('No MOD editor identity on CDC')


def stop(port):
    answer = exchange(port, 'MOD STOP')
    if answer != 'OK MOD QUEUED':
        raise RuntimeError(answer)
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        state = exchange(port, 'MOD STATUS')
        if 'running=0' in state:
            return state
        time.sleep(.05)
    raise TimeoutError('Playback did not stop')


def upload(port, source, discard_edits=False):
    data = source.read_bytes()
    if len(data) < 2108 or len(data) > LIMIT:
        raise ValueError(f'MOD must be 2108..{LIMIT} bytes')
    state = stop(port)
    if 'dirty=1' in state and not discard_edits:
        raise RuntimeError('Unsaved FM-1 edits; save first or use --discard-edits')
    crc = zlib.crc32(data)
    if exchange(port, f'MOD BEGIN {len(data):08x} {crc:08x}') != 'OK MOD BEGIN RAM':
        raise RuntimeError('Upload was not accepted')
    try:
        for offset in range(0, len(data), 64):
            chunk = data[offset:offset + 64]
            answer = exchange(port, f'MOD DATA {offset:08x} {chunk.hex()}')
            if answer != f'OK MOD DATA {offset + len(chunk):08x}':
                raise RuntimeError(f'Offset mismatch: {answer}')
        if exchange(port, 'MOD END') != 'OK MOD READY RAM VOLATILE':
            raise RuntimeError('MOD parser rejected upload')
    except Exception:
        try:
            exchange(port, 'MOD ABORT')
        finally:
            raise
    return len(data), crc


def save(port, destination):
    if destination.exists():
        raise FileExistsError(destination)
    stop(port)
    info = exchange(port, 'MOD INFO')
    match = re.fullmatch(r'MOD INFO ([0-9a-fA-F]{8}) ([0-9a-fA-F]{8}) ([01])', info)
    if not match:
        raise RuntimeError(f'Unexpected MOD INFO: {info}')
    length, expected = int(match[1], 16), int(match[2], 16)
    if length < 2108 or length > LIMIT:
        raise RuntimeError('Device reported invalid MOD length')
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix=destination.name + '.', suffix='.tmp',
                                         dir=destination.parent, delete=False) as output:
            temporary = Path(output.name)
            crc = 0
            for offset in range(0, length, 64):
                count = min(64, length - offset)
                answer = exchange(port, f'MOD READ {offset:08x} {count:02x}')
                prefix = f'MOD DATA {offset:08x} '
                if not answer.startswith(prefix):
                    raise RuntimeError(f'Unexpected data at {offset}: {answer}')
                chunk = bytes.fromhex(answer[len(prefix):])
                if len(chunk) != count:
                    raise RuntimeError(f'Invalid data length at {offset}')
                output.write(chunk)
                crc = zlib.crc32(chunk, crc)
            output.flush()
            os.fsync(output.fileno())
        if crc != expected:
            raise RuntimeError(f'CRC mismatch: {crc:08x} != {expected:08x}')
        if destination.exists():
            raise FileExistsError(destination)
        os.rename(temporary, destination)
        temporary = None
        if exchange(port, f'MOD SAVED {crc:08x}') != 'OK MOD SAVED':
            raise RuntimeError('File saved locally, but FM-1 did not acknowledge it')
        return length, crc
    except Exception:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
        try:
            exchange(port, 'MOD ABORT')
        except Exception:
            pass
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('list', 'status', 'key', 'vol', 'upload', 'save', 'new', 'play', 'stop'))
    parser.add_argument('--port')
    parser.add_argument('--file', type=Path)
    parser.add_argument('--discard-edits', action='store_true')
    args = parser.parse_args()
    from serial import Serial
    from serial.tools import list_ports
    if args.action == 'list':
        for item in list_ports.comports():
            print(item.device, item.description, f'{item.vid:04x}:{item.pid:04x}' if item.vid is not None else '')
        return
    if not args.port:
        parser.error('--port is required')
    if args.action in ('upload', 'save') and not args.file:
        parser.error('--file is required')
    with closing(Serial(port=None, baudrate=115200, timeout=.5, write_timeout=5)) as port:
        port.port = args.port
        port.dtr = True
        port.rts = False
        port.open()
        port.reset_input_buffer()
        # The FM-1 CDC endpoint can drop a command sent immediately on open.
        time.sleep(.2)
        identify(port)
        if args.action == 'upload':
            size, crc = upload(port, args.file, args.discard_edits)
            print(f'Loaded {size} bytes into volatile RAM, CRC {crc:08x}')
        elif args.action == 'save':
            size, crc = save(port, args.file)
            print(f'Saved {size} bytes to {args.file}, CRC {crc:08x}')
        elif args.action == 'stop':
            print(stop(port))
        else:
            command = {'status': 'MOD STATUS', 'key': 'MOD KEY', 'vol': 'MOD VOL',
                       'new': 'MOD NEW', 'play': 'MOD PLAY'}[args.action]
            print(exchange(port, command))


if __name__ == '__main__':
    main()
