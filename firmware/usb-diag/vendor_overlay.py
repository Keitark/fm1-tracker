"""Isolated, exact-pin SDK CDC corrections. Never modifies the vendor checkout."""
import re

def nes_render_skip(text):
    """NROM/CNROM hidden-frame pixels only; retain all emulation side effects."""
    text=once(text,'#include "nes.h"', '''#include "nes.h"
#include "fm1_nes.h"
static int fm1_render_pixels=1;

/* Hidden frames only need background opacity underneath sprite zero. The
   FM-1 port accepts only NROM/CNROM (no PPU-fetch mapper side effects). Keep
   the general renderer for any other mapper. Sprite evaluation/overflow,
   sprite-zero testing and scroll/CPU/APU scheduling remain upstream code. */
static void fm1_hidden_background(nes_t *nes,unsigned scanline) {
    const sprite_info_t *s=&nes->nes_ppu.sprite_info[0];
    unsigned x,end,sy=(unsigned)s->y+1u;
    unsigned height=nes->nes_ppu.CTRL_H?16u:8u;
    if(!nes->nes_ppu.MASK_s || nes->nes_ppu.STATUS_S || s->y>=0xefu ||
       scanline<sy || scanline>=sy+height)return;
    end=(unsigned)s->x+8u;if(end>256u)end=256u;
    for(x=s->x;x<end;++x) {
        unsigned sx=(unsigned)nes->nes_ppu.v.coarse_x*8u+nes->nes_ppu.x+x;
        unsigned nt=nes->nes_ppu.v.nametable ^ (sx>>8);
        unsigned tile=nes->nes_ppu.name_table[nt][nes->nes_ppu.v.coarse_y*32u+((sx&255u)>>3)];
        unsigned bank=(nes->nes_ppu.CTRL_B?4u:0u)+(tile>>6);
        const uint8_t *p=nes->nes_ppu.pattern_table[bank]+((tile&63u)<<4)+nes->nes_ppu.v.fine_y;
        nes->nes_ppu.bg_opaque[x]=(x<8u && !nes->nes_ppu.MASK_m)?0u:
            (uint8_t)(((p[0]|p[8])>>(7u-(sx&7u)))&1u);
    }
}''')
    text=once(text,'    while(!nes->nes_quit){', '''    while(!nes->nes_quit){
        fm1_render_pixels=fm1_nes_render_frame();
        if(nes->nes_rom.mapper_number!=0 && nes->nes_rom.mapper_number!=3)
            fm1_render_pixels=1; /* No assumptions about other mapper hooks. */''')
    text=once(text,'static void nes_render_background_line(nes_t* nes,uint16_t scanline,nes_color_t* draw_data){',
        '''static void nes_render_background_line(nes_t* nes,uint16_t scanline,nes_color_t* draw_data){
    if(!fm1_render_pixels){fm1_hidden_background(nes,scanline);return;}''')
    text=once(text,'''#if (NES_FRAME_SKIP != 0)
        if(nes->nes_frame_skip_count == 0)
#endif
        {
            uint8_t p = sprite_info.x;''', '''        if(fm1_render_pixels)
        {
            uint8_t p = sprite_info.x;''')
    text=once(text,'                nes_memset(nes->nes_draw_data, nes->nes_ppu.background_palette[0], sizeof(nes_color_t) * NES_DRAW_SIZE);',
        '                if(fm1_render_pixels)nes_memset(nes->nes_draw_data, nes->nes_ppu.background_palette[0], sizeof(nes_color_t) * NES_DRAW_SIZE);')
    text=once(text,'                for (uint16_t x = 0; x < NES_WIDTH; x++) {',
        '                if(fm1_render_pixels)for (uint16_t x = 0; x < NES_WIDTH; x++) {')
    for call in ('nes_draw(0, 0, NES_WIDTH-1, NES_HEIGHT/2-1, nes->nes_draw_data);',
                 'nes_draw(0, NES_HEIGHT/2, NES_WIDTH-1, NES_HEIGHT-1, nes->nes_draw_data);'):
        text=once(text,call,'if(fm1_render_pixels)'+call)
    text=once(text,'            nes_draw(0, 0, NES_WIDTH-1, NES_HEIGHT-1, nes->nes_draw_data);',
        '            if(fm1_render_pixels)nes_draw(0, 0, NES_WIDTH-1, NES_HEIGHT-1, nes->nes_draw_data);')
    return text

def nes_tiles(text):
    """Bit-identical bitplane decoding; preserve all fetch/mapper/timing code."""
    names=('nes_render_background_line','nes_render_sprite_line')
    originals=[]
    for name in names:
        hits=list(re.finditer(r'static void '+name+r'\([^\n]*\)\{.*?^\}',text,re.M|re.S))
        if len(hits)!=1:raise ValueError('Tile renderer anchor changed: '+name)
        originals.append(hits[0].group())
    table=[sum(((x>>i)&1)<<(2*i) for i in range(8)) for x in range(256)]
    rows=['    '+','.join('0x%04x'%x for x in table[i:i+16])+',' for i in range(0,256,16)]
    helper='''
/* Two 8-bit pattern planes -> eight packed 2-bit pixels. Immutable decoded
 * byte table, NOT a CHR/palette cache: bank switches and RAM writes stay live.
 * Target placement is initialized internal RAM, checked by the ELF audit. */
#ifdef FM1_TARGET_PI32V2
__attribute__((section(".data.fm1_tile_expand"),aligned(4),used))
#endif
static const uint16_t fm1_tile_expand[256]={
'''+ '\n'.join(rows)+'''
};
#ifdef FM1_TILE_TESTING
int fm1_tile_test_reference;
static void fm1_reference_background(nes_t*,uint16_t,nes_color_t*);
#endif
static inline uint8_t fm1_tile_background_span(nes_t *nes,nes_color_t *dst,
        uint8_t p,uint8_t high,uint8_t b0,uint8_t b1,int first,int last) {
    unsigned bits=fm1_tile_expand[b0]|(fm1_tile_expand[b1]<<1);
    if(first==7 && last==0 && (p>=8 || nes->nes_ppu.MASK_m)) {
        const nes_color_t *pal=nes->nes_ppu.background_palette+high;
        uint8_t *opaque=nes->nes_ppu.bg_opaque+p;dst+=p;
'''
    for i in range(8):
        helper+='        {unsigned c=(bits>>%du)&3u;dst[%d]=pal[c];opaque[%d]=(uint8_t)(c!=0);}\n'%(14-2*i,i,i)
    helper+='''        return (uint8_t)(p+8);
    }
    /* First/last partial tile and left-eight clipping keep the reference
     * pixel helper. No out-of-row loads/stores on the fast path. */
    for(;first>=last;--first) {
        nes_draw_background_pixel(nes,dst,p,(uint8_t)(high|((bits>>(2*first))&3u)));++p;
    }
    return p;
}
'''
    anchor='static void nes_render_background_line(nes_t* nes,uint16_t scanline,nes_color_t* draw_data){'
    text=once(text,anchor,helper+'\n'+anchor+'''
#ifdef FM1_TILE_TESTING
    if(fm1_tile_test_reference){fm1_reference_background(nes,scanline,draw_data);return;}
#endif''')
    text=once(text,'''        for (; m >= 0; m--){
            uint8_t low_bit = ((bit0 >> m) & 0x01) | ((bit1 >> m)<<1 & 0x02);
            nes_draw_background_pixel(nes, draw_data, p, high_bit | low_bit);
            p++;
        }''','''        p=fm1_tile_background_span(nes,draw_data,p,high_bit,bit0,bit1,m,0);''')
    text=once(text,'''        for (; m >= skew; m--){
            const uint8_t low_bit = ((bit0 >> m) & 0x01) | ((bit1 >> m)<<1 & 0x02);
            nes_draw_background_pixel(nes, draw_data, p, high_bit | low_bit);
            p++;
        }''','''        p=fm1_tile_background_span(nes,draw_data,p,high_bit,bit0,bit1,m,skew);''')
    anchor='static void nes_render_sprite_line(nes_t* nes,const sprite_line_t* sprite_line,nes_color_t* draw_data){'
    text=once(text,anchor,'''#ifdef FM1_TILE_TESTING
static void fm1_reference_sprite(nes_t*,const sprite_line_t*,nes_color_t*);
#endif
'''+anchor+'''
#ifdef FM1_TILE_TESTING
    if(fm1_tile_test_reference){fm1_reference_sprite(nes,sprite_line,draw_data);return;}
#endif''')
    anchor='            uint8_t p = sprite_info.x;'
    text=once(text,anchor,anchor+'''
            const unsigned fm1_sprite_bits=fm1_tile_expand[sprite_bit0]|(fm1_tile_expand[sprite_bit1]<<1);''')
    anchor='const uint8_t low_bit = ((sprite_bit0 >> m) & 0x01) | ((sprite_bit1 >> m)<<1 & 0x02);'
    if text.count(anchor)!=2:raise ValueError('Sprite decode anchor changed')
    text=text.replace(anchor,'const uint8_t low_bit = (fm1_sprite_bits>>(2*m))&3u;')
    # Host-only oracle is the exact pre-optimization generated renderer.
    text+='\n#ifdef FM1_TILE_TESTING\n'+originals[0].replace(names[0],'fm1_reference_background',1)+'\n'
    text+=originals[1].replace(names[1],'fm1_reference_sprite',1)+'\n#endif\n'
    return text


def nes_apu(text):
    """Capture four pre-mix voices without changing the pinned APU algorithms."""
    text=once(text,'#include "nes.h"', '''#include "nes.h"
#include "fm1_nes.h"
static uint8_t fm1_voice_samples[4][NES_APU_SAMPLE_PER_SYNC];''')
    anchor='        uint16_t mixed = (uint16_t)((247 * ((uint16_t)p1_out + p2_out) + 279 * tri_out + 162 * noi_out) >> 7);'
    text=once(text,anchor,'''        fm1_voice_samples[0][i]=p1_out;
        fm1_voice_samples[1][i]=p2_out;
        fm1_voice_samples[2][i]=tri_out;
        fm1_voice_samples[3][i]=noi_out;
'''+anchor)
    text=once(text,'        nes_sound_output(apu->sample_buffer, NES_APU_SAMPLE_PER_SYNC);','''        const uint8_t *voices[4]={fm1_voice_samples[0],fm1_voice_samples[1],fm1_voice_samples[2],fm1_voice_samples[3]};
        fm1_nes_sound_channels(apu->sample_buffer,voices,NES_APU_SAMPLE_PER_SYNC);''')
    return text

def nes_profile(text):
    """Wrap call boundaries only; no instruction/scanline/audio changes."""
    text=once(text,'#include "nes.h"', '''#include "nes.h"
#include "fm1_profile.h"
static void fm1_profile_opcode(nes_t *n,uint16_t ticks) {
    FM1_PROFILE_BEGIN(FM1_PROF_CPU);nes_opcode(n,ticks);FM1_PROFILE_END();
}
static void fm1_profile_apu(nes_t *n) {
    FM1_PROFILE_BEGIN(FM1_PROF_APU);nes_apu_frame(n);FM1_PROFILE_END();
}
#define nes_opcode fm1_profile_opcode
#define nes_apu_frame fm1_profile_apu''')
    return once(text,'    while(!nes->nes_quit){', '''    while(!nes->nes_quit){
        (void)fm1_profile_enter(FM1_PROF_PPU);''')


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Pinned USB source changed: ' + old[:80])
    return text.replace(old,new)

def function(text, name, replacement):
    pattern=r'^(?:static )?(?:u32|void|int) '+name+r'\([^\n]*\)\n\{.*?^\}'
    hits=list(re.finditer(pattern,text,re.M|re.S))
    if len(hits)!=1:raise ValueError('Expected one function: '+name)
    m=hits[0]
    return text[:m.start()]+replacement+text[m.end():]

def boot_entry(text):
    return once(text,'void __attribute__((weak)) nvram_set_boot_state(u32 state) {}',
                'extern void nvram_set_boot_state(u32 state);')

def cdc(text):
    text='#include "usb_control.h"\n#include "boot_entry.h"\n'+text
    text=once(text,'    u8 bmTransceiver;','    volatile u8 bmTransceiver;')
    text=once(text,'static struct usb_cdc_gadget *cdc_hdl[USB_MAX_HW_NUM];',
        'static struct usb_cdc_gadget *cdc_hdl[USB_MAX_HW_NUM];\nvolatile unsigned fm1_cdc_generation;')
    # There are two request decoders in the vendor file; replace the receiver
    # first, leaving a single setup-stage insertion point.
    text=function(text,'cdc_setup_rx', '''static u32 cdc_setup_rx(struct usb_device_t *usb_device, struct usb_ctrlrequest *ctrl_req)
{
    const usb_dev usb_id=usb_device2id(usb_device);
    u8 data[7];
    if(!fm1_cdc_request_valid(ctrl_req->bRequestType,ctrl_req->bRequest,
                             ctrl_req->wValue,ctrl_req->wIndex,ctrl_req->wLength) ||
       ctrl_req->bRequest!=USB_CDC_REQ_SET_LINE_CODING ||
       !cdc_hdl[usb_id] || usb_read_count0(usb_id)!=7) {
        usb_set_setup_phase(usb_device,USB_EP0_SET_STALL); return USB_EP0_SET_STALL;
    }
    usb_read_ep0(usb_id,data,sizeof(data));
    memcpy(cdc_hdl[usb_id]->subtype_data,data,sizeof(data));
    return USB_EP0_STAGE_SETUP;
}''')
    text=once(text,'    recip_type = ctrl_req->bRequestType & USB_TYPE_MASK;',
        '''    if(!fm1_cdc_request_valid(ctrl_req->bRequestType,ctrl_req->bRequest,
                                  ctrl_req->wValue,ctrl_req->wIndex,ctrl_req->wLength)) {
        usb_set_setup_phase(usb_device, USB_EP0_SET_STALL); return 0;
    }
    recip_type = ctrl_req->bRequestType & USB_TYPE_MASK;''')
    begin=text.index('                /* if (ctrl_req->wValue & BIT(0))')
    end=text.index('            }',begin)
    text=text[:begin]+'''                if((cdc_hdl[usb_id]->bmTransceiver & 3)!=(ctrl_req->wValue & 3))
                    ++fm1_cdc_generation;
                cdc_hdl[usb_id]->bmTransceiver=(ctrl_req->wValue & 3) | BIT(4);
'''+text[end:]
    # Reset invalidates the protocol session even if DTR never went low.
    text=once(text,'#if USB_ROOT2\n    usb_disable_ep(usb_id, CDC_DATA_EP_IN);',
        '    if(cdc_hdl[usb_id])cdc_hdl[usb_id]->bmTransceiver=0;\n    ++fm1_cdc_generation;\n#if USB_ROOT2\n    usb_disable_ep(usb_id, CDC_DATA_EP_IN);')
    text=function(text,'cdc_wakeup_handler', '''static void cdc_wakeup_handler(struct usb_device_t *usb_device, u32 ep)
{
    extern void fm1_usb_rx_irq(struct usb_device_t *);
    (void)ep;
    fm1_usb_rx_irq(usb_device);
}''')
    text=function(text,'cdc_read_data', '''u32 cdc_read_data(const usb_dev usb_id, u8 *buf, u32 len)
{
    if(usb_id!=FM1_USB_CONTROLLER)return 0;
    return fm1_usb_rx_take(buf,len);
}''')
    # Bounded task-only mutex wait and no multi-packet/ZLP loop.
    text=function(text,'cdc_write_data', '''u32 cdc_write_data(const usb_dev usb_id, u8 *buf, u32 len)
{
    u32 result;
    if(!cdc_hdl[usb_id] || !len || len>=MAXP_SIZE_CDC_BULKIN ||
       (cdc_hdl[usb_id]->bmTransceiver & (BIT(0)|BIT(4)))!=(BIT(0)|BIT(4)) ||
       usb_id2device(usb_id)->bDeviceStates!=USB_CONFIGURED)return 0;
    if(os_mutex_pend(&cdc_hdl[usb_id]->mutex_data,1))return 0;
    if(usb_read_txcsr(usb_id,CDC_DATA_EP_IN)&1)result=0;
    else result=usb_g_bulk_write(usb_id,CDC_DATA_EP_IN,buf,len);
    os_mutex_post(&cdc_hdl[usb_id]->mutex_data);
    return result;
}''')
    # Unused generic echo prints untrusted bytes as a C string: remove it.
    text=re.sub(r'^s32 usb_cdc_output_handler\(void \*priv, u8 \*buf, u32 len\)\n\{.*?^\}',
                '',text,flags=re.M|re.S)
    text+='''
int fm1_cdc_allocated(usb_dev id) {
    return cdc_hdl[id] && cdc_hdl[id]->cdc_buffer && cdc_hdl[id]->bulk_ep_in_buffer &&
           cdc_hdl[id]->bulk_ep_out_buffer && cdc_hdl[id]->intr_ep_in_buffer;
}
int fm1_cdc_ready(usb_dev id) {
    return fm1_cdc_allocated(id) && usb_id2device(id)->bDeviceStates==USB_CONFIGURED &&
           (cdc_hdl[id]->bmTransceiver & 17)==17;
}
'''
    return text

def device(text):
    # CDC-only: do not pull video/audio/host header trees into this build.
    for name in ('usb/device/msd.h','usb/scsi.h','usb/device/hid.h','usb/device/uac_audio.h',
                 'usb/device/slave_uvc.h','usb/device/printer.h'):
        text=once(text,'#include "'+name+'"\n','')
    text='extern int fm1_cdc_allocated(unsigned char);\n'+text
    text=once(text,'static void usb_device_init(const usb_dev usb_id)',
                    'static int usb_device_init(const usb_dev usb_id)')
    text=once(text,'    usb_config(usb_id);', '''    if(usb_config(usb_id))return -1;
    if(!ep0_dma_buffer[usb_id])ep0_dma_buffer[usb_id]=usb_alloc_ep_dmabuffer(usb_id,0,64);
    if(!ep0_dma_buffer[usb_id])return -2;
    user_setup_filter_install(usb_id2device(usb_id));''')
    text=once(text,'    usb_g_isr_reg(usb_id, 3, 0);','    usb_g_isr_reg(usb_id, 3, 0);\n    return 0;')
    text=once(text,'    usb_device_init(usb_id);\n    user_setup_filter_install(usb_id2device(usb_id));\n    return 0;',
                    '    if(!fm1_cdc_allocated(usb_id))return -3;\n    return usb_device_init(usb_id);')
    return text

if __name__=='__main__':
    import argparse
    from pathlib import Path
    p=argparse.ArgumentParser(description='Generate a build-only APU overlay; no vendor edits')
    group=p.add_mutually_exclusive_group(required=True)
    group.add_argument('--nes-apu',type=Path);group.add_argument('--nes-render',type=Path)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--profile',action='store_true')
    p.add_argument('--tiles',action='store_true')
    a=p.parse_args();a.out.parent.mkdir(parents=True,exist_ok=True)
    source=a.nes_apu or a.nes_render
    result=(nes_apu if a.nes_apu else nes_render_skip)(source.read_text(encoding='utf-8'))
    if a.tiles:
        if not a.nes_render:p.error('--tiles requires --nes-render')
        result=nes_tiles(result)
    if a.profile:
        if not a.nes_render:p.error('--profile requires --nes-render')
        result=nes_profile(result)
    a.out.write_text(result,encoding='utf-8')
