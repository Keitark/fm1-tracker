"""Receive-only Windows MIDI note capture. No MIDI output, SysEx or flash access.

Uses midiInOpen / MidiInProc documented by Microsoft:
https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-midiinopen
https://learn.microsoft.com/en-us/previous-versions/dd798460(v=vs.85)
MIDI pitches are NOT the FM-1's internal key scanner IDs.
"""
import argparse
import ctypes
from ctypes import wintypes as w
from datetime import datetime, timezone
import json
from pathlib import Path
import queue
import time


def decode_note(packed, milliseconds):
    status=packed&255
    note=(packed>>8)&255
    velocity=(packed>>16)&255
    if status&240 not in (0x80,0x90) or note>127 or velocity>127:
        return None
    return dict(event='note_on' if status&240==0x90 and velocity else 'note_off',
                channel=(status&15)+1,note=note,velocity=velocity,
                pitch_class=('C','C#','D','D#','E','F','F#','G','G#','A','A#','B')[note%12],
                device_ms=milliseconds)


class Caps(ctypes.Structure):
    _fields_=[('manufacturer',w.WORD),('product',w.WORD),('version',w.DWORD),
              ('name',w.WCHAR*32),('support',w.DWORD)]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port',default='FM-1 Midi',help='Exact MIDI input name; never chooses another device')
    ap.add_argument('--seconds',type=int,default=90)
    ap.add_argument('--list',action='store_true')
    ap.add_argument('--output',type=Path)
    args=ap.parse_args()
    if not 1<=args.seconds<=300: ap.error('Duration must be 1..300 seconds')
    lib=ctypes.WinDLL('winmm')
    callback_type=ctypes.WINFUNCTYPE(None,ctypes.c_void_p,w.UINT,ctypes.c_size_t,ctypes.c_size_t,ctypes.c_size_t)
    lib.midiInGetNumDevs.restype=w.UINT
    lib.midiInGetDevCapsW.argtypes=[ctypes.c_size_t,ctypes.POINTER(Caps),w.UINT]
    lib.midiInGetDevCapsW.restype=w.UINT
    lib.midiInOpen.argtypes=[ctypes.POINTER(ctypes.c_void_p),w.UINT,ctypes.c_size_t,ctypes.c_size_t,w.DWORD]
    lib.midiInOpen.restype=w.UINT
    for name in ('midiInStart','midiInStop','midiInReset','midiInClose'):
        getattr(lib,name).argtypes=[ctypes.c_void_p]
        getattr(lib,name).restype=w.UINT
    def check(result,operation):
        if result: raise RuntimeError(f'{operation}: WinMM error {result}')
    ports=[]
    for index in range(lib.midiInGetNumDevs()):
        caps=Caps()
        check(lib.midiInGetDevCapsW(index,ctypes.byref(caps),ctypes.sizeof(caps)),'midiInGetDevCapsW')
        ports.append(dict(id=index,name=caps.name))
    if args.list:
        print(json.dumps(ports,ensure_ascii=True));return
    matches=[p for p in ports if p['name']==args.port]
    if len(matches)!=1: raise SystemExit(f'Need one exact match for {args.port!r}; available: {ports}')
    messages=queue.SimpleQueue()
    @callback_type
    def callback(handle,message,instance,data,ms):
        # Never invoke multimedia APIs or perform file I/O from this callback.
        if message in (0x3c3,0x3cc,0x3c5): messages.put((message,data,ms))
    handle=ctypes.c_void_p()
    check(lib.midiInOpen(ctypes.byref(handle),matches[0]['id'],
                        ctypes.cast(callback,ctypes.c_void_p).value,0,0x30000),'midiInOpen')
    output=args.output or Path(__file__).resolve().parents[2]/'tmp'/('fm1-notes-'+datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S-%f')+'.jsonl')
    count=0
    try:
        output.parent.mkdir(parents=True,exist_ok=True)
        with output.open('x',encoding='utf-8') as log:
            def emit(event):
                text=json.dumps(event,ensure_ascii=True)
                print(text,flush=True);log.write(text+'\n');log.flush()
            emit(dict(event='capture',port=args.port,receive_only=True,seconds=args.seconds,
                      started_utc=datetime.now(timezone.utc).isoformat(),output=str(output)))
            check(lib.midiInStart(handle),'midiInStart')
            print('LISTENING: from the far-left F, tap and release F F# G G# A A# B C C# D D# E, one at a time.',flush=True)
            deadline=time.monotonic()+args.seconds
            while time.monotonic()<deadline:
                try: message,data,ms=messages.get(timeout=min(0.25,max(0.001,deadline-time.monotonic())))
                except queue.Empty: continue
                if message==0x3c5: emit(dict(event='midi_input_error',device_ms=ms));continue
                event=decode_note(data,ms)
                if event:
                    if event['event']=='note_on': count+=1
                    emit(event)
            emit(dict(event='finished',note_on_count=count,scanner_ids_verified=False))
    finally:
        # These reset only the host input/capture handle, not the synth's state.
        failures=[]
        for name in ('midiInStop','midiInReset','midiInClose'):
            result=getattr(lib,name)(handle)
            if result: failures.append(f'{name}: {result}')
        if failures: raise RuntimeError('; '.join(failures))


if __name__=='__main__': main()
