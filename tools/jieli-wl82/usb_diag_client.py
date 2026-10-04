"""Normal-user CDC diagnostic client. No elevation/raw-disk/flash operations.

The firmware supports transport verification, NOT installation.
"""
import argparse
from pathlib import Path
import time
import zlib

IDENTITY=(0x3654,0x5155)  # private lab use of the pinned SDK identity
HELLO='FM1DIAG/1 USB-ONLY UPDATE=VERIFY-ONLY COMMIT=BLOCKED'
LIMIT=512*1024

def exchange(port,command,deadline=3):
    data=(command+'\n').encode('ascii')
    if port.write(data)!=len(data):raise RuntimeError('Short serial write; transfer aborted')
    end=time.monotonic()+deadline;line=bytearray()
    while time.monotonic()<end:
        c=port.read(1)
        if not c:continue
        if c==b'\n':
            answer=line.decode('ascii',errors='strict').rstrip('\r');line.clear()
            if answer.startswith('#') or not answer:continue
            if answer.startswith('ERR'):raise RuntimeError(answer)
            return answer
        line.extend(c)
        if len(line)>256:raise RuntimeError('Oversized reply; wrong device or lost framing')
    raise TimeoutError('No complete reply; nothing was installed')

def verify_transfer(port,data):
    if not 0<len(data)<=LIMIT:raise ValueError('Expected 1..524288 bytes; raw 1 MiB dumps are not accepted')
    if exchange(port,'HELLO')!=HELLO:raise RuntimeError('Unexpected firmware/protocol; refusing transfer')
    if exchange(port,f'BEGIN {len(data):08x} {zlib.crc32(data):08x}')!='OK BEGIN VERIFY-ONLY':
        raise RuntimeError('Unexpected BEGIN reply')
    for offset in range(0,len(data),128):
        chunk=data[offset:offset+128]
        if exchange(port,f'DATA {offset:08x} {chunk.hex()}')!=f'OK DATA {offset+len(chunk):08x}':
            raise RuntimeError('Offset acknowledgement mismatch')
    if exchange(port,'END')!='OK VERIFIED TRANSPORT-ONLY NOT-STORED NOT-INSTALLED':
        raise RuntimeError('Unexpected verification result')
    return 'Transport CRC/length matched. Data was NOT stored or installed; flash commit is blocked.'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action',choices=('list','info','verify'))
    p.add_argument('--port',help='Explicit COM port; identity is checked before opening')
    p.add_argument('--file',type=Path,help='Application file for a VERIFY-ONLY transfer')
    a=p.parse_args()
    import serial
    from serial.tools import list_ports
    ports=list(list_ports.comports())
    if a.action=='list':
        for item in ports:
            if (item.vid,item.pid)==IDENTITY:print(item.device,item.description)
        return
    if not a.port:p.error('--port is required')
    matches=[x for x in ports if x.device.casefold()==a.port.casefold() and (x.vid,x.pid)==IDENTITY]
    if len(matches)!=1:raise SystemExit('Port does not match the diagnostic VID/PID; no port opened')
    data=None
    if a.action=='verify':
        if not a.file:p.error('--file is required for verify')
        if not 0<a.file.stat().st_size<=LIMIT:raise SystemExit('File size rejected before opening a port')
        data=a.file.read_bytes()
    with serial.Serial(a.port,115200,timeout=0.1,write_timeout=2,rtscts=False,dsrdtr=False) as port:
        port.dtr=True;port.rts=False
        if a.action=='info':
            result=exchange(port,'HELLO')
            if result!=HELLO:raise RuntimeError('Unexpected firmware identity')
            print(result);print(exchange(port,'STATUS'))
        else:
            try:print(verify_transfer(port,data))
            except Exception:
                try:exchange(port,'ABORT',deadline=1)
                except Exception:pass
                raise

if __name__=='__main__':main()
