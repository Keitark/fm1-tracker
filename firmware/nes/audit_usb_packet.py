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

def audit_usb_packet(symbols,code_at,composite=True):
    def require(ok,msg):
        if not ok:raise ValueError('USB packet '+msg)
    require('fm1_usb_packet_write' in symbols,'commit missing')
    for name in ('usb_g_bulk_write','usb_g_iso_write','usb_g_ep_write'):
        require(name not in symbols,'polling writer linked: '+name)
    start,size,_=symbols['fm1_usb_packet_write']
    require(size==(158 if composite else 156),'commit size changed')
    calls=CALLS if composite else ((0x30,6,'__local_irq_disable'),(0x38,4,'usb_id2device'),
       (0x46,4,'usb_read_txcsr'),(0x5a,2,'usb_get_dma_taddr'),
       (0x68,6,'memmove'),(0x8a,2,'usb_write_txcsr'),(0x8e,6,'__local_irq_enable'))
    short=SHORT if composite else {0x5a:(bytes.fromhex('41 90'),-224),0x8a:(bytes.fromhex('41 86'),-244)}
    digest=BODY_SHA256 if composite else '132aa8a42844f92fa65336381fc28eb82e7be54304967425bed0a76f4ad905a0'
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
