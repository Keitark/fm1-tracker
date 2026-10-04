"""FM1 MDX CDC loader/control. Song RAM only; no flash or firmware updates."""
import argparse
from pathlib import Path
import struct
import sys
import time
import zlib
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools/jieli-wl82'))
from usb_diag_client import exchange,IDENTITY
LIMIT=192*1024
HELLO='FM1DIAG/1 MDX-KARAOKE/1 RAM-UPLOAD UBOOT=SERIAL COMMIT=BLOCKED'

def bundle(mdx,pdx=b''):
    if not mdx or len(mdx)+len(pdx)+12>LIMIT:
        raise ValueError('MDX + PDX +12-byte header must fit192KiB')
    return b'FM1M'+struct.pack('<II',len(mdx),len(pdx))+mdx+pdx

def wait_stopped(port):
    end=time.monotonic()+5
    while time.monotonic()<end:
        answer=exchange(port,'MDX STATUS')
        if 'running=0 ' in answer and 'pending=0 ' in answer:return
        time.sleep(.02)
    raise TimeoutError('Player did not stop; no song bytes sent')

def send(port,data):
    if exchange(port,'HELLO')!=HELLO:raise RuntimeError('Wrong firmware; no song uploaded')
    exchange(port,'MDX STOP');wait_stopped(port)
    if exchange(port,f'MDX BEGIN {len(data):08x} {zlib.crc32(data):08x}')!='OK MDX BEGIN RAM':
        raise RuntimeError('Unexpected upload acknowledgement')
    try:
        for offset in range(0,len(data),120):
            chunk=data[offset:offset+120]
            answer=exchange(port,f'MDX DATA {offset:08x} {chunk.hex()}')
            if answer!=f'OK MDX DATA {offset+len(chunk):08x}':raise RuntimeError('Offset mismatch')
        if exchange(port,'MDX END')!='OK MDX STORED RAM VOLATILE':raise RuntimeError('File not accepted')
    except BaseException:
        try:exchange(port,'MDX ABORT')
        except Exception:pass
        raise
    return 'Song loaded into RAM; lost on power-off. Use play to start.'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action',choices=('list','upload','status','play','stop','demo','select','mute','note'))
    p.add_argument('--port');p.add_argument('--mdx',type=Path);p.add_argument('--pdx',type=Path)
    p.add_argument('--track',type=int,help='1..8 for select;1..16 for mute')
    p.add_argument('--note',type=int,help='MIDI13..108');p.add_argument('--on',type=int,choices=(0,1),default=1)
    a=p.parse_args();data=None
    if a.action=='upload':
        if not a.mdx:p.error('--mdx is required')
        if a.mdx.stat().st_size>LIMIT:p.error('MDX too large')
        if a.pdx and a.pdx.stat().st_size>LIMIT:p.error('PDX too large')
        data=bundle(a.mdx.read_bytes(),a.pdx.read_bytes() if a.pdx else b'')
    if a.action in ('select','mute') and (a.track is None or not 1<=a.track<=(8 if a.action=='select' else 16)):
        p.error('--track must be1..8 (select) or1..16 (mute)')
    if a.action=='note' and (a.note is None or not 13<=a.note<=108):p.error('--note must be13..108')
    import serial
    from serial.tools import list_ports
    ports=[x for x in list_ports.comports() if (x.vid,x.pid)==IDENTITY]
    if a.action=='list':
        for x in ports:print(x.device,x.description)
        return
    if not a.port or sum(x.device.casefold()==a.port.casefold() for x in ports)!=1:
        p.error('Specify the FM1 CDC port with --port; no port opened')
    with serial.Serial(a.port,115200,timeout=.1,write_timeout=2,rtscts=False,dsrdtr=False) as port:
        port.dtr=True;port.rts=False
        if exchange(port,'HELLO')!=HELLO:raise RuntimeError('MDX firmware identity mismatch')
        if data is not None:print(send(port,data));return
        command='MDX '+a.action.upper()
        if a.action=='select':command+=f' {a.track-1}'
        elif a.action=='mute':command+=f' {a.track-1:02x} {a.on}'
        elif a.action=='note':command+=f' {a.note:02x} {a.on}'
        print(exchange(port,command))
        if a.action=='stop':wait_stopped(port)
        print(exchange(port,'MDX STATUS'))
if __name__=='__main__':main()
