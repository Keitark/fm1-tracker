"""Original FM1 karaoke demo, with three FM parts and one generated PDX drum."""
from pathlib import Path
import struct

def loop(track, start):
    return bytes(track)+b'\xf1'+struct.pack('>h',start-len(track)-3)

def voice(number,algorithm):
    return bytes([number,algorithm,15,1,2,1,1,24,32,40,0,
                  31,31,31,31,8,8,8,8,4,4,4,4,15,15,15,15])

def make():
    tracks=[]
    for ch,notes in enumerate(((47,51,54,59,54,51,49,47),(23,23,30,30,28,28,25,25),(35,39,42,39,40,44,47,44))):
        t=bytearray([255,200,253,0,252,(3,1,2)[ch],251,(12,9,7)[ch]])
        start=len(t)
        for i,note in enumerate(notes):
            # Change the melody patch halfway through every loop.
            if ch==0 and i==4:t.extend([253,1,251,11])
            if ch==0 and i==0:t.extend([253,0,251,12])
            t.extend([128+note,23])
        tracks.append(loop(t,start))
    tracks.extend([b'\xf1\0']*5)
    t=bytearray([252,3,251,10,237,4]);start=len(t)
    for _ in range(8):t.extend([128,5,17])
    tracks.append(loop(t,start))
    cursor=20;offsets=[]
    for t in tracks:offsets.append(cursor);cursor+=len(t)
    mdx=b'FM1 original karaoke demo\r\n\x1aFM1DEMO\0'+struct.pack('>10H',cursor,*offsets)+b''.join(tracks)+voice(0,7)+voice(1,4)
    drum=bytes([0x77]*32+[0xff]*32+[0x17,0x9f]*48)
    pdx=struct.pack('>II',768,len(drum))+bytes(760)+drum
    return mdx,pdx

def generate_c(path):
    mdx,pdx=make()
    out=['#include <stddef.h>']
    for name,data in [('mdx',mdx),('pdx',pdx)]:
        out.append('const unsigned char fm1_demo_'+name+'[]={'+','.join(str(v) for v in data)+'};')
        out.append('const size_t fm1_demo_'+name+'_size=sizeof(fm1_demo_'+name+');')
    Path(path).parent.mkdir(parents=True,exist_ok=True)
    Path(path).write_text('\n'.join(out)+'\n')

if __name__=='__main__':
    mdx,pdx=make();base=Path(__file__).resolve().parent
    (base/'FM1DEMO.MDX').write_bytes(mdx);(base/'FM1DEMO.PDX').write_bytes(pdx)
    generate_c(base/'demo.c')
