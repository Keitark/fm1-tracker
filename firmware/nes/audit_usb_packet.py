# SPDX-License-Identifier: GPL-3.0-only
# Reused from Keitark/fm1-mdx at 4141057; see licenses/fm1-mdx-GPL-3.0.txt.
"""Pin the reviewed USB0 single-packet commit and reject SDK polling writers."""
import hashlib
from audit_power import call_target

BODY_SHA256='ddd3ecf77a811a0db509e32581aced7cb9a9bbb0af7aa1b7252f6d23669809ec'
CALLS=((0x30,6,'__local_irq_disable'),(0x38,4,'usb_id2device'),
       (0x48,4,'usb_read_txcsr'),(0x5c,2,'usb_get_dma_taddr'),
       (0x6a,6,'memmove'),(0x8c,2,'usb_write_txcsr'),(0x90,6,'__local_irq_enable'))
# Short two-byte calls retain exact encodings and checked target distances.
SHORT={0x5c:(bytes.fromhex('41 8f'),-226),0x8c:(bytes.fromhex('41 85'),-246)}

def audit_usb_packet(symbols,code_at,composite=True,video=False,midi=False):
    def require(ok,msg):
        if not ok:raise ValueError('USB packet '+msg)
    require('fm1_usb_packet_write' in symbols,'commit missing')
    for name in ('usb_g_bulk_write','usb_g_iso_write','usb_g_ep_write'):
        require(name not in symbols,'polling writer linked: '+name)
    start,size,_=symbols['fm1_usb_packet_write']
    require(size==(162 if midi and composite else 158 if composite else 156),'commit size changed')
    calls=CALLS if composite else ((0x30,6,'__local_irq_disable'),(0x38,4,'usb_id2device'),
       (0x46,4,'usb_read_txcsr'),(0x5a,2,'usb_get_dma_taddr'),
       (0x68,6,'memmove'),(0x8a,2,'usb_write_txcsr'),(0x8e,6,'__local_irq_enable'))
    short=SHORT if composite else {0x5a:(bytes.fromhex('41 90'),-224),0x8a:(bytes.fromhex('41 86'),-244)}
    digest=BODY_SHA256 if composite else '132aa8a42844f92fa65336381fc28eb82e7be54304967425bed0a76f4ad905a0'
    if midi and composite:
        # The MIDI adapter makes these shared SDK helpers more distant. Exact
        # 2026-10-18 r3 disassembly comparison with the accepted 2026-10-12
        # composite link has the same 51 semantic instructions and branch
        # destinations: only two 2-byte calls widen to 4 bytes and relative
        # branch distances change. EP1/192 or EP3/<64 bounds, IRQ nesting,
        # epoch/DMA checks, exact EP1/3 count addresses, csync and ICFG restore
        # remain fully hashed. Every widened call is independently targeted.
        calls=((0x30,6,'__local_irq_disable'),(0x38,4,'usb_id2device'),
               (0x48,4,'usb_read_txcsr'),(0x5c,4,'usb_get_dma_taddr'),
               (0x6c,6,'memmove'),(0x8e,4,'usb_write_txcsr'),(0x94,6,'__local_irq_enable'))
        short={}
        digest='ec7526fe5eb7bbcef376fc2a4290d4c322e6e422dd1359eecd20d13c88ce944e'
    if video:
        # packet.c is unchanged and still permits only EP1/192 or EP3/<64.
        # The additional EP4 adapter shares SDK register helpers, so LTO no
        # longer emits the legacy short-call layout. These two complete
        # normalized bodies were reviewed from the optional-UVC links; keep
        # the existing no-video whitelist intact. In particular the bounds,
        # epoch/DMA checks, exact EP1/3 doorbells, csync and ICFG restore stay
        # fully hashed. Only independently target-checked calls are normalized.
        calls=((0x30,6,'__local_irq_disable'),(0x38,4,'usb_id2device'),
               (0x48,2,'usb_read_txcsr'),(0x5a,2,'usb_get_dma_taddr'),
               (0x68,6,'memmove'),(0x8a,4,'usb_write_txcsr'),(0x90,6,'__local_irq_enable')) if composite else (
               (0x30,6,'__local_irq_disable'),(0x38,4,'usb_id2device'),
               (0x46,2,'usb_read_txcsr'),(0x58,2,'usb_get_dma_taddr'),
               (0x66,6,'memmove'),(0x88,4,'usb_write_txcsr'),(0x8e,6,'__local_irq_enable'))
        short={0x48:(bytes.fromhex('41 96'),-212),0x5a:(bytes.fromhex('61 84'),-120)} if composite else {
               0x46:(bytes.fromhex('51 80'),-192),0x58:(bytes.fromhex('61 85'),-118)}
        digest='f2cb5c8ded832160dc2da1b496711b0340a6d1108245684f7e3f2bc83a36e5a8' if composite else '4d4a58736a7ef962d042a75151cd4afda4338edf87f8f12a061dbb7badaf399b'
    body=bytearray(code_at(start,size))
    for off,n,name in calls:
        require(name in symbols,'dependency missing: '+name)
        instruction=bytes(body[off:off+n])
        if n==2:
            expected,displacement=short[off]
            require(instruction==expected,'short call changed: '+name)
            target=start+off+2+displacement
        else:
            target=call_target(instruction,start+off)
        require(target==symbols[name][0],'call target changed: '+name)
        body[off:off+n]=bytes(n)
    require(hashlib.sha256(body).hexdigest()==digest,'commit instructions changed')
    return {'commit_bytes':size,'sdk_polling_writers':False,'hardware_verified':False}
