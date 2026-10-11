#include "fake_uac_sdk.h"
#include "../fm1_usb_audio.h"
#include "../fm1_usb_audio_target.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if(!(condition)) { \
    fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#condition); exit(1); } } while(0)

unsigned fm1_uac_test_irq_mask;
static unsigned lock_held,irq_nesting,reentry_probe,reentry_opportunities;
static struct usb_device_t device;
static itf_hander handlers[6];
static itf_reset_hander resets[6];
static usb_interrupt completion;
static uint32_t txcsr,packet_length,max_packet;
static uint32_t arm_count,sync_count,disable_count,config_count,payload_count;
static unsigned interrupt_enabled,endpoint_enabled;
static uint8_t *dma;
static uint8_t in_bytes[2],other_dma[4][256];
static uint32_t in_length;
static unsigned fail_interface=99,fail_reset=99;

unsigned fm1_usb_audio_test_irq_save(void)
{unsigned old=fm1_uac_test_irq_mask;CHECK(!lock_held);fm1_uac_test_irq_mask=1;return old;}
void fm1_usb_audio_test_irq_restore(unsigned old)
{CHECK(!lock_held);fm1_uac_test_irq_mask=old;}
void fm1_usb_audio_test_lock(unsigned char *p)
{CHECK(p && !*p && fm1_uac_test_irq_mask && !lock_held);*p=1;lock_held=1;}
void fm1_usb_audio_test_unlock(unsigned char *p)
{CHECK(p && *p && lock_held && fm1_uac_test_irq_mask);*p=0;lock_held=0;}

void sys_local_irq_disable(void){++irq_nesting;fm1_uac_test_irq_mask=1;}
void sys_local_irq_enable(void)
{CHECK(irq_nesting);if(--irq_nesting==0)fm1_uac_test_irq_mask=0;}
usb_dev usb_device2id(const struct usb_device_t *d)
{CHECK(!lock_held);return d->usb_id;}
void usb_set_setup_phase(struct usb_device_t *d,u8 phase)
{CHECK(!lock_held);d->bsetup_phase=phase;}
void usb_clr_intr_txe(usb_dev id,u32 ep)
{CHECK(!lock_held && id==0 && ep==1);interrupt_enabled=0;}
u32 usb_g_set_intr_hander(usb_dev id,u32 ep,usb_interrupt fn)
{CHECK(!lock_held && id==0 && ep==0x81);completion=fn;return ep&15u;}
void usb_disable_ep(usb_dev id,u32 ep)
{CHECK(!lock_held && id==0 && ep==1);endpoint_enabled=0;++disable_count;}
void usb_enable_ep(usb_dev id,u32 ep)
{CHECK(!lock_held && id==0 && ep==1);endpoint_enabled=1;}
void usb_write_txcsr(usb_dev id,u32 ep,u32 value)
{
    CHECK(!lock_held && id==0 && ep==1);
    sys_local_irq_disable(); /* SDK register helpers use counted IRQ sections. */
    if(value&1u){CHECK(endpoint_enabled && interrupt_enabled && dma);++arm_count;}
    txcsr=value;
    sys_local_irq_enable();
    if(reentry_probe && !fm1_uac_test_irq_mask)++reentry_opportunities;
}
u32 usb_read_txcsr(usb_dev id,u32 ep)
{CHECK(!lock_held && id==0 && ep==1);return txcsr;}
void usb_write_ep_cnt(usb_dev id,u32 ep,u32 length)
{CHECK(!lock_held && id==0 && ep==1 && length<=180);packet_length=length;}
void usb_set_dma_taddr(usb_dev id,u32 ep,void *p)
{CHECK(!lock_held && id==0 && ep==1 && p && !((uintptr_t)p&63u));dma=p;}
u32 usb_g_ep_config(usb_dev id,u32 ep,u32 type,u32 irq,u8 *p,u32 length)
{
    CHECK(!lock_held && id==0 && ep==0x81 && type==1 && irq==1 && length==180);
    CHECK(!(txcsr&1u) && p && !((uintptr_t)p&63u));
    if(dma)CHECK(dma==p);
    dma=p;txcsr=BIT(14);interrupt_enabled=1;max_packet=1023;++config_count;return 0;
}
void usb_write_txmaxp(usb_dev id,u32 ep,u32 value)
{CHECK(!lock_held && id==0 && ep==1 && value==180);max_packet=value;}
u8 *usb_set_data_payload(struct usb_device_t *d,struct usb_ctrlrequest *r,const void *bytes,u32 length)
{
    CHECK(!lock_held && length<=2 && length==r->wLength);
    memcpy(in_bytes,bytes,length);in_length=length;++payload_count;
    d->bsetup_phase=USB_EP0_STAGE_IN;return in_bytes;
}
u32 usb_set_interface_hander(usb_dev id,u32 itf,itf_hander fn)
{
    CHECK(!lock_held && id==0 && (itf==FM1_USB_AUDIO_AC_INTERFACE || itf==FM1_USB_AUDIO_AS_INTERFACE));
    if(itf==fail_interface)return 99;
    handlers[itf]=fn;return itf;
}
u32 usb_set_reset_hander(usb_dev id,u32 itf,itf_reset_hander fn)
{
    CHECK(!lock_held && id==0 && itf==FM1_USB_AUDIO_AC_INTERFACE);
    if(itf==fail_reset)return 99;
    resets[itf]=fn;return itf;
}
void fm1_uac_test_sync(void){CHECK(!lock_held);++sync_count;}
static u32 other_setup(struct usb_device_t *d,struct usb_ctrlrequest *r)
{(void)d;(void)r;CHECK(0);return 0;}
static void other_reset(struct usb_device_t *d,u32 itf)
{(void)d;(void)itf;CHECK(0);}

static fm1_usb_audio_capture_diagnostics diagnostics(void)
{
    fm1_usb_audio_capture_diagnostics d;
    unsigned mask=fm1_uac_test_irq_mask;
    fm1_usb_audio_capture_get_diagnostics(&d);
    CHECK(!lock_held && fm1_uac_test_irq_mask==mask && d.fill<=FM1_USB_AUDIO_FIFO_FRAMES);
    return d;
}
static struct usb_ctrlrequest request(uint8_t type,uint8_t operation,uint16_t value,
                                     uint16_t index,uint16_t length)
{
    struct usb_ctrlrequest r;
    r.bRequestType=type;r.bRequest=operation;r.wValue=value;r.wIndex=index;r.wLength=length;
    return r;
}
static int setup(struct usb_ctrlrequest *r)
{
    int result;
    unsigned mask=fm1_uac_test_irq_mask;
    device.bsetup_phase=USB_EP0_STAGE_SETUP;in_length=0;
    result=fm1_usb_audio_setup_filter(&device,r);
    CHECK(!lock_held && fm1_uac_test_irq_mask==mask);return result;
}
static void configuration(uint16_t value)
{
    struct usb_ctrlrequest r=request(0,9,value,0,0);
    if(device.bDeviceStates!=USB_CONFIGURED)device.bDeviceStates=USB_ADDRESS;
    CHECK(setup(&r)==0);
    device.bDeviceStates=value?USB_CONFIGURED:USB_ADDRESS;
}
static void alternate(uint16_t value,int valid)
{
    struct usb_ctrlrequest r=request(1,11,value,FM1_USB_AUDIO_AS_INTERFACE,0);
    CHECK(setup(&r)==1);
    CHECK(device.bsetup_phase==(valid?USB_EP0_STAGE_SETUP:USB_EP0_SET_STALL));
}
static void other_unchanged(void)
{
    unsigned i,j;
    for(i=0;i<FM1_USB_AUDIO_AC_INTERFACE;++i) {
        CHECK(handlers[i]==other_setup && resets[i]==other_reset);
        for(j=0;j<256;++j)CHECK(other_dma[i][j]==(uint8_t)(i*31u+j));
    }
}
static void fixture(void)
{
    unsigned i,j;
    CHECK(!lock_held && !irq_nesting);
    memset(&device,0,sizeof(device));device.bDeviceStates=USB_ADDRESS;
    memset(handlers,0,sizeof(handlers));memset(resets,0,sizeof(resets));
    for(i=0;i<FM1_USB_AUDIO_AC_INTERFACE;++i) {
        handlers[i]=other_setup;resets[i]=other_reset;
        for(j=0;j<256;++j)other_dma[i][j]=(uint8_t)(i*31u+j);
    }
    completion=NULL;dma=NULL;
    txcsr=packet_length=max_packet=arm_count=sync_count=disable_count=config_count=payload_count=0;
    interrupt_enabled=endpoint_enabled=in_length=0;
    fm1_uac_test_irq_mask=0;fail_interface=fail_reset=99;
    fm1_usb_audio_target_init();
    CHECK(diagnostics().armed && !diagnostics().active);
}
static void descriptor(void)
{
    uint8_t guarded[101];u32 interface_number=FM1_USB_AUDIO_AC_INTERFACE;
    unsigned offset=0,endpoints=0;
    memset(guarded,0xa5,sizeof(guarded));
    CHECK(fm1_uac_desc_config(0,guarded+1,&interface_number)==99);
    CHECK(interface_number==FM1_USB_AUDIO_AS_INTERFACE+1 && guarded[0]==0xa5 && guarded[100]==0xa5);
    CHECK(!memcmp(guarded+1,fm1_usb_audio_capture_descriptor,99));
    while(offset<99) {
        const uint8_t *d=guarded+1+offset;
        CHECK(d[0]>=2 && offset+d[0]<=99);
        if(d[1]==5) {
            CHECK(d[0]==9 && d[2]==0x81 && d[3]==5 && d[4]==180 && !d[5]);
            ++endpoints;
        }
        offset+=d[0];
    }
    CHECK(offset==99 && endpoints==1);
    CHECK(handlers[FM1_USB_AUDIO_AC_INTERFACE] && handlers[FM1_USB_AUDIO_AS_INTERFACE]);
    CHECK(resets[FM1_USB_AUDIO_AC_INTERFACE]);other_unchanged();
}
static void start(void)
{
    configuration(1);alternate(1,1);
    CHECK(diagnostics().active && max_packet==180 && dma && endpoint_enabled && interrupt_enabled);
    CHECK(packet_length==176 && (txcsr&1u) && arm_count==1 && sync_count==1);
    CHECK(diagnostics().silent_frames==44 && diagnostics().submitted_packets==1);
}
static void produce(unsigned seed)
{
    int32_t pcm[128],original[128];unsigned i;
    for(i=0;i<64;++i) {
        pcm[2*i]=(int32_t)(0x1234u+seed)*256;
        pcm[2*i+1]=-(int32_t)(0x2345u+seed)*256;
    }
    memcpy(original,pcm,sizeof(pcm));
    fm1_usb_audio_capture_push_pcm24(pcm,64);
    CHECK(!memcmp(original,pcm,sizeof(pcm)));
}
static void transaction(void)
{
    usb_interrupt fn=completion;
    CHECK(fn && endpoint_enabled && interrupt_enabled && (txcsr&1u));
    txcsr&=~1u;fn(&device,1);
}
static void control_contract(void)
{
    struct usb_ctrlrequest r;unsigned i;
    uint32_t arms=arm_count,configs=config_count,payloads;
    fm1_usb_audio_capture_diagnostics before=diagnostics();
    static const uint16_t lengths[]={0,1,2,3,180,65535};
    uint8_t snapshot[256];memcpy(snapshot,dma,sizeof(snapshot));
    for(i=0;i<sizeof(lengths)/sizeof(lengths[0]);++i) {
        r=request(0x21,1,0x0100,FM1_USB_AUDIO_AC_INTERFACE,lengths[i]);
        payloads=payload_count;CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL);
        CHECK(payload_count==payloads);
        r=request(0xa1,0x81,0x0100,0x0300|FM1_USB_AUDIO_AC_INTERFACE,lengths[i]);
        CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL && payload_count==payloads);
        r=request(0x22,1,0x0100,0x81,lengths[i]);
        CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL && payload_count==payloads);
    }
    r=request(0xc1,0x81,0,FM1_USB_AUDIO_AS_INTERFACE,1);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL);
    r=request(0xc1,0x81,0,0x0100|FM1_USB_AUDIO_AS_INTERFACE,1);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL);
    r=request(0x81,10,0,0x0100|FM1_USB_AUDIO_AS_INTERFACE,1);CHECK(setup(&r)==0);
    r=request(1,11,1,FM1_USB_AUDIO_AC_INTERFACE,0);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL);
    r=request(1,11,2,FM1_USB_AUDIO_AS_INTERFACE,0);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL);
    r=request(1,11,1,FM1_USB_AUDIO_AS_INTERFACE,1);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL);
    r=request(0x81,10,1,FM1_USB_AUDIO_AS_INTERFACE,1);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL);
    r=request(0x81,0,0,FM1_USB_AUDIO_AC_INTERFACE,1);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL);
    r=request(0x81,10,0,FM1_USB_AUDIO_AS_INTERFACE,1);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_STAGE_IN && in_length==1 && in_bytes[0]==1);
    r=request(0x81,10,0,FM1_USB_AUDIO_AC_INTERFACE,1);
    CHECK(setup(&r)==1 && in_length==1 && in_bytes[0]==0);
    r=request(0x81,0,0,FM1_USB_AUDIO_AS_INTERFACE,2);
    CHECK(setup(&r)==1 && in_length==2 && in_bytes[0]==0 && in_bytes[1]==0);
    r=request(0x82,0,0,0x81,2);
    CHECK(setup(&r)==1 && in_length==2 && in_bytes[0]==0 && in_bytes[1]==0);
    r=request(2,3,0,0x81,0);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL);
    r=request(0xa1,0x21,0,0,7);CHECK(setup(&r)==0); /* CDC request. */
#ifdef FM1_UVC_SCREEN
    r=request(0xa1,0x81,0x0100,3,26);CHECK(setup(&r)==0); /* UVC request. */
#endif
    r=request(0,9,2,0,0);CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL);
    r=request(0,9,1,1,0);CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL);
    r=request(0,9,1,0,1);CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL);
    CHECK(arm_count==arms && config_count==configs && !memcmp(snapshot,dma,sizeof(snapshot)));
    CHECK(diagnostics().epoch==before.epoch && diagnostics().fill==before.fill);
    other_unchanged();
}
static void descriptor_contract(void)
{
    uint8_t out[99],saved[99];u32 itf;
    fixture();memset(out,0xa5,sizeof(out));memcpy(saved,out,sizeof(out));
    itf=FM1_USB_AUDIO_AC_INTERFACE;
    CHECK(!fm1_uac_desc_config(1,out,&itf) && itf==FM1_USB_AUDIO_AC_INTERFACE && !memcmp(out,saved,99));
    itf=0;CHECK(!fm1_uac_desc_config(0,out,&itf) && itf==0 && !memcmp(out,saved,99));
    itf=FM1_USB_AUDIO_AC_INTERFACE;CHECK(!fm1_uac_desc_config(0,NULL,&itf));
    CHECK(!fm1_uac_desc_config(0,out,NULL));other_unchanged();
    fail_interface=FM1_USB_AUDIO_AC_INTERFACE;CHECK(!fm1_uac_desc_config(0,out,&itf));
    CHECK(!memcmp(out,saved,99) && itf==FM1_USB_AUDIO_AC_INTERFACE);other_unchanged();
    fixture();itf=FM1_USB_AUDIO_AC_INTERFACE;fail_reset=FM1_USB_AUDIO_AC_INTERFACE;
    CHECK(!fm1_uac_desc_config(0,out,&itf));CHECK(!memcmp(out,saved,99));other_unchanged();
}
static void dma_and_lifecycle(void)
{
    uint8_t snapshot[256];uint32_t arms,packets,i;
    fm1_usb_audio_capture_diagnostics before,after;
    struct usb_ctrlrequest r;
    fixture();descriptor();
    device.bDeviceStates=USB_DEFAULT;r=request(0,9,1,0,0);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL);
    alternate(1,0);start();control_contract();
    produce(0);produce(0);memcpy(snapshot,dma,sizeof(snapshot));arms=arm_count;before=diagnostics();
    completion(&device,1);after=diagnostics();
    CHECK(arm_count==arms && !memcmp(snapshot,dma,sizeof(snapshot)));
    CHECK(after.fill==before.fill && after.busy==before.busy+1 && after.sent_frames==before.sent_frames);
    txcsr|=BIT(7)|BIT(2);completion(&device,1);
    CHECK(arm_count==arms+1 && !memcmp(snapshot,dma,sizeof(snapshot)) && (txcsr&1u));
    CHECK(!(txcsr&(BIT(7)|BIT(2))) && diagnostics().submitted_packets==before.submitted_packets);
    transaction();CHECK(packet_length==176 && diagnostics().fill==84);
    for(i=0;i<44;++i) {
        CHECK(dma[4*i]==0x34 && dma[4*i+1]==0x12);
        CHECK(dma[4*i+2]==0xbb && dma[4*i+3]==0xdc);
    }
    for(i=packet_length;i<sizeof(snapshot);++i)CHECK(dma[i]==snapshot[i]);
    packets=diagnostics().submitted_packets;completion(&device,4);
    CHECK(diagnostics().submitted_packets==packets);
    device.usb_id=1;completion(&device,1);device.usb_id=0;
    CHECK(diagnostics().submitted_packets==packets);
    before=diagnostics();alternate(0,1);after=diagnostics();
    CHECK(!after.active && after.armed && !after.fill && !after.pending_bytes && after.epoch!=before.epoch);
    CHECK(!completion && !endpoint_enabled && !interrupt_enabled && !(txcsr&1u) && !packet_length);
    alternate(1,1);CHECK(diagnostics().active && max_packet==180);
    resets[FM1_USB_AUDIO_AC_INTERFACE](&device,UINT32_MAX);
    CHECK(!diagnostics().active && !completion && !endpoint_enabled);
    alternate(1,0);configuration(1);alternate(1,1);
    configuration(1); /* Reconfiguration stops live capture even if value is unchanged. */
    CHECK(!diagnostics().active && !completion && !endpoint_enabled);
    alternate(1,1);configuration(0);
    CHECK(!diagnostics().active && !completion && !endpoint_enabled);
    alternate(1,0);configuration(1);alternate(1,1);
    produce(0);before=diagnostics();arms=arm_count;
    fm1_uac_test_irq_mask=0;reentry_probe=1;fm1_usb_audio_target_stop();reentry_probe=0;
    CHECK(!irq_nesting && !lock_held && !reentry_opportunities && fm1_uac_test_irq_mask==0);
    after=diagnostics();CHECK(!after.armed && !after.active && !after.fill && !after.pending_bytes);
    CHECK(after.epoch!=before.epoch && !completion && !endpoint_enabled && !interrupt_enabled);
    configuration(1);alternate(1,0);resets[FM1_USB_AUDIO_AC_INTERFACE](&device,UINT32_MAX);
    configuration(1);alternate(1,0);produce(0);
    CHECK(arm_count==arms && !diagnostics().fill && !completion);
    fm1_usb_audio_target_init();configuration(1);alternate(1,1);
    CHECK(diagnostics().active && arm_count==arms+1);
    fm1_uac_test_irq_mask=1;fm1_usb_audio_target_stop();
    CHECK(fm1_uac_test_irq_mask==1 && !irq_nesting && !completion);
    fm1_uac_test_irq_mask=0;other_unchanged();
}
static void status_contract(void)
{
    char out[512],tiny[10];
    fixture();descriptor();start();produce(0);produce(0);transaction();
    memset(out,0xa5,sizeof(out));fm1_usb_audio_target_status(out,sizeof(out));
    CHECK(strstr(out,"USB AUDIO rate=44100 bits=16 ch=2 active=1 armed=1 cfg=1 shutdown=0"));
    CHECK(strstr(out,"pushed=128 packets=2 sent=88 silent=44"));
    CHECK(strlen(out)<384 && out[strlen(out)-1]=='\n' && (unsigned char)out[384]==0xa5);
    memset(tiny,0xa5,sizeof(tiny));fm1_usb_audio_target_status(tiny+1,8);
    CHECK((unsigned char)tiny[0]==0xa5 && tiny[8]==0 && (unsigned char)tiny[9]==0xa5);
    fm1_usb_audio_target_status(NULL,384);fm1_usb_audio_target_status(tiny,0);
    CHECK(!lock_held && !fm1_uac_test_irq_mask && !irq_nesting);fm1_usb_audio_target_stop();
}
int main(void)
{
    descriptor_contract();dma_and_lifecycle();status_contract();
    puts("UAC adapter: descriptor/control routing, stereo DMA, busy/incomplete ISO, maxp, config/reset/close/reopen, permanent shutdown and bounded status passed");
    return 0;
}
