#include "fake_uvc_sdk.h"
#include "../fm1_uvc.h"
#include "../uvc_control.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if(!(condition)) { \
    fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#condition); exit(1); } } while(0)

unsigned fm1_uvc_test_irq_mask;
static struct usb_device_t device;
static itf_hander handlers[4], receive;
static itf_reset_hander reset_handler;
static usb_interrupt completion;
static uint32_t clock_ms=10, txcsr, count0, packet_length, max_packet;
static uint32_t arm_count, sync_count, read_count, disable_count, hardware_stalls;
static unsigned interrupt_enabled, endpoint_enabled;
static uint8_t *dma;
static uint8_t out_bytes[26], in_bytes[26];
static uint32_t in_length;
static unsigned irq_nesting,reentry_probe,reentry_opportunities;

void sys_local_irq_disable(void){++irq_nesting;fm1_uvc_test_irq_mask=1;}
void sys_local_irq_enable(void)
{CHECK(irq_nesting);if(--irq_nesting==0)fm1_uvc_test_irq_mask=0;}

uint32_t timer_get_ms(void){return clock_ms;}
usb_dev usb_device2id(const struct usb_device_t *d){return d->usb_id;}
void usb_set_setup_phase(struct usb_device_t *d,u8 phase){d->bsetup_phase=phase;}
void usb_clr_intr_txe(usb_dev id,u32 ep){CHECK(id==0 && ep==4);interrupt_enabled=0;}
u32 usb_g_set_intr_hander(usb_dev id,u32 ep,usb_interrupt fn)
{CHECK(id==0 && ep==0x84);completion=fn;return ep&15u;}
void usb_disable_ep(usb_dev id,u32 ep)
{CHECK(id==0 && ep==4);endpoint_enabled=0;++disable_count;}
void usb_enable_ep(usb_dev id,u32 ep){CHECK(id==0 && ep==4);endpoint_enabled=1;}
void usb_write_txcsr(usb_dev id,u32 ep,u32 value)
{
    sys_local_irq_disable(); /* SDK register helpers nest these calls. */
    CHECK(id==0 && ep==4);
    if(value&1u){CHECK(endpoint_enabled && interrupt_enabled && dma);++arm_count;}
    txcsr=value;
    sys_local_irq_enable();
    if(reentry_probe && !fm1_uvc_test_irq_mask)++reentry_opportunities;
}
u32 usb_read_txcsr(usb_dev id,u32 ep){CHECK(id==0 && ep==4);return txcsr;}
void usb_write_ep_cnt(usb_dev id,u32 ep,u32 length)
{CHECK(id==0 && ep==4 && length<=962);packet_length=length;}
void usb_set_dma_taddr(usb_dev id,u32 ep,void *p)
{CHECK(id==0 && ep==4);dma=p;}
u32 usb_g_ep_config(usb_dev id,u32 ep,u32 type,u32 irq,u8 *p,u32 length)
{
    CHECK(id==0 && ep==0x84 && type==1 && irq==1 && length==962);
    CHECK(!(txcsr&1u));dma=p;txcsr=BIT(14);interrupt_enabled=1;return 0;
}
void usb_write_txmaxp(usb_dev id,u32 ep,u32 value)
{CHECK(id==0 && ep==4);max_packet=value;}
u32 usb_read_count0(usb_dev id){CHECK(id==0);return count0;}
void usb_read_ep0(usb_dev id,u8 *bytes,u32 length)
{CHECK(id==0 && length==26 && count0==26);memcpy(bytes,out_bytes,26);++read_count;}
void usb_ep0_Set_Stall(usb_dev id){CHECK(id==0);++hardware_stalls;}
u8 *usb_set_data_payload(struct usb_device_t *d,struct usb_ctrlrequest *r,const void *bytes,u32 length)
{(void)r;CHECK(length<=26);memcpy(in_bytes,bytes,length);in_length=length;d->bsetup_phase=USB_EP0_STAGE_IN;return in_bytes;}
void usb_set_setup_recv(struct usb_device_t *d,itf_hander fn){(void)d;receive=fn;}
u32 usb_set_interface_hander(usb_dev id,u32 itf,itf_hander fn)
{CHECK(id==0 && (itf==2 || itf==3));handlers[itf]=fn;return itf;}
u32 usb_set_reset_hander(usb_dev id,u32 itf,itf_reset_hander fn)
{CHECK(id==0 && itf==2);reset_handler=fn;return itf;}
void fm1_uvc_test_sync(void){++sync_count;}

static struct usb_ctrlrequest request(uint8_t type,uint8_t operation,uint16_t value,
                                     uint16_t index,uint16_t length)
{
    struct usb_ctrlrequest r;
    r.bRequestType=type;r.bRequest=operation;r.wValue=value;r.wIndex=index;r.wLength=length;
    return r;
}
static int setup(struct usb_ctrlrequest *r)
{
    device.bsetup_phase=USB_EP0_STAGE_SETUP;in_length=0;
    return fm1_uvc_setup_filter(&device,r);
}
static void configure(uint16_t value)
{
    struct usb_ctrlrequest r=request(0,9,value,0,0);
    if(device.bDeviceStates!=USB_CONFIGURED)device.bDeviceStates=USB_ADDRESS;
    CHECK(setup(&r)==0);
    device.bDeviceStates=value?USB_CONFIGURED:USB_ADDRESS;
}
static void commit(void)
{
    struct usb_ctrlrequest r=request(0xa1,FM1_UVC_GET_DEF,0x0100,3,26);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_STAGE_IN && in_length==26);
    memcpy(out_bytes,in_bytes,26);
    r=request(0x21,FM1_UVC_SET_CUR,0x0200,3,26);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_STAGE_OUT && receive);
    count0=26;
    CHECK(receive(&device,&r)==USB_EP0_STAGE_SETUP);
}
static void alternate(uint16_t value,int valid)
{
    struct usb_ctrlrequest r=request(1,11,value,3,0);
    CHECK(setup(&r)==1);
    CHECK(device.bsetup_phase==(valid?USB_EP0_STAGE_SETUP:USB_EP0_SET_STALL));
}
static void complete_transaction(void)
{
    usb_interrupt fn=completion;
    CHECK(fn && endpoint_enabled && interrupt_enabled && (txcsr&1u));
    txcsr&=~1u;
    fn(&device,4);
}
static fm1_uvc_write queue_rows(uint8_t value)
{
    fm1_uvc_write write;
    fm1_uvc_stream *s=fm1_uvc_stream_get();
    CHECK(fm1_uvc_begin_frame(s,clock_ms)==FM1_UVC_OK);
    CHECK(fm1_uvc_reserve_rows(s,clock_ms,2,&write)==FM1_UVC_OK);
    memset(write.bytes+2,value,write.length-2u);
    CHECK(fm1_uvc_publish_rows(s,clock_ms,&write)==FM1_UVC_OK);
    return write;
}

static void read_error(uint8_t expected)
{
    struct usb_ctrlrequest r=request(0xa1,FM1_UVC_GET_CUR,0x0200,2,1);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_STAGE_IN &&
          in_length==1 && in_bytes[0]==expected);
    CHECK(setup(&r)==1 && in_length==1 && in_bytes[0]==FM1_UVC_ERROR_NONE);
}

static void vc_error_queries(void)
{
    struct usb_ctrlrequest r;
    fm1_uvc_status prior,after;
    uint32_t arms=arm_count;
    uint32_t reads=read_count;
    unsigned i;
    CHECK(fm1_uvc_get_status(&prior)==1);

    memset(in_bytes,0xa5,sizeof(in_bytes));
    r=request(0xa1,FM1_UVC_GET_INFO,0x0200,2,1);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_STAGE_IN &&
          in_length==1 && in_bytes[0]==1); /* The VC error control is GET-only. */
    for(i=1;i<sizeof(in_bytes);++i)CHECK(in_bytes[i]==0xa5);

    r=request(0xa1,FM1_UVC_GET_CUR,0x0200,2,1);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_STAGE_IN &&
          in_length==1 && in_bytes[0]==FM1_UVC_ERROR_NONE);
    for(i=1;i<sizeof(in_bytes);++i)CHECK(in_bytes[i]==0xa5);
    r=request(0xa1,FM1_UVC_GET_INFO,0x0200,2,255);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_STAGE_IN &&
          in_length==1 && in_bytes[0]==1);
    for(i=1;i<sizeof(in_bytes);++i)CHECK(in_bytes[i]==0xa5);

    /* An unsupported selector still stalls; its error can then be read once. */
    r=request(0xa1,FM1_UVC_GET_CUR,0x9900,2,1);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL && in_length==0);
    r=request(0xa1,FM1_UVC_GET_CUR,0x0200,2,1);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_STAGE_IN &&
          in_length==1 && in_bytes[0]==FM1_UVC_ERROR_INVALID_CONTROL);
    CHECK(setup(&r)==1 && in_length==1 && in_bytes[0]==FM1_UVC_ERROR_NONE);

    /* A zero-length query and a SET to the GET-only control remain invalid. */
    r=request(0xa1,FM1_UVC_GET_INFO,0x0200,2,0);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL && in_length==0);
    r=request(0x21,FM1_UVC_SET_CUR,0x0200,2,1);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL && in_length==0);
    r=request(0xa1,FM1_UVC_GET_CUR,0x0200,2,1);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_STAGE_IN &&
          in_length==1 && in_bytes[0]==FM1_UVC_ERROR_INVALID_REQUEST);

    /* Class wIndex carries an entity in its high byte. Known terminals have
     * no controls; unknown VC/VS entities report INVALID_UNIT through EP0. */
    r=request(0xa1,FM1_UVC_GET_CUR,0x0200,0x0102,1);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL && in_length==0);
    read_error(FM1_UVC_ERROR_INVALID_CONTROL);
    r=request(0xa1,FM1_UVC_GET_CUR,0x0200,0x0302,1);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL && in_length==0);
    read_error(FM1_UVC_ERROR_INVALID_UNIT);
    r=request(0x21,FM1_UVC_SET_CUR,0x0200,0x0103,26);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL && in_length==0);
    read_error(FM1_UVC_ERROR_INVALID_UNIT);

    /* Entity routing does not extend the standard/vendor interface whitelist.
     * Returning zero leaves these high-index requests to the existing policy. */
    r=request(0x81,10,0,0x0102,1);
    CHECK(setup(&r)==0 && in_length==0);
    r=request(0xc1,FM1_UVC_GET_CUR,0x0200,0x0102,1);
    CHECK(setup(&r)==0 && in_length==0);
    r=request(0xc1,FM1_UVC_GET_CUR,0x0200,2,1);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL && in_length==0);

    CHECK(fm1_uvc_get_status(&after)==1 && arm_count==arms && read_count==reads);
    CHECK(prior.active==after.active && prior.committed==after.committed &&
          prior.alt==after.alt && prior.configured==after.configured &&
          prior.packets_sent==after.packets_sent);
}

static void malformed_out_queries(void)
{
    static const uint32_t lengths[]={0,25,27,65535};
    struct usb_ctrlrequest r;
    fm1_uvc_status prior,after;
    uint8_t previous[1024];
    uint32_t arms=arm_count,reads=read_count,csr=txcsr,bytes=packet_length;
    unsigned i;
    CHECK(fm1_uvc_get_status(&prior)==1);
    if(dma)memcpy(previous,dma,sizeof(previous));
    for(i=0;i<sizeof(lengths)/sizeof(lengths[0]);++i) {
        uint32_t stalls=hardware_stalls;
        r=request(0x21,FM1_UVC_SET_CUR,0x0200,3,26);
        CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_STAGE_OUT && receive);
        count0=lengths[i];
        CHECK(receive(&device,&r)==USB_EP0_SET_STALL && hardware_stalls==stalls+1);
        read_error(FM1_UVC_ERROR_INVALID_REQUEST);
    }

    /* A superseded malformed OUT must preserve the newer request's error. */
    r=request(0x21,FM1_UVC_SET_CUR,0x0200,3,26);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_STAGE_OUT);
    {struct usb_ctrlrequest newer=request(0xa1,FM1_UVC_GET_CUR,0x0200,0x0302,1);
     CHECK(setup(&newer)==1 && device.bsetup_phase==USB_EP0_SET_STALL);}
    count0=25;
    CHECK(receive(&device,&r)==USB_EP0_SET_STALL);
    read_error(FM1_UVC_ERROR_INVALID_UNIT);

    CHECK(fm1_uvc_get_status(&after)==1 && arm_count==arms && read_count==reads);
    CHECK(txcsr==csr && packet_length==bytes);
    if(dma)CHECK(!memcmp(previous,dma,sizeof(previous)));
    CHECK(prior.active==after.active && prior.committed==after.committed &&
          prior.alt==after.alt && prior.configured==after.configured &&
          prior.packets_sent==after.packets_sent);
}

int main(void)
{
    uint8_t descriptors[159], previous[1024];
    uint32_t itf=2, before, reads;
    struct usb_ctrlrequest r;
    fm1_uvc_write reservation;
    fm1_uvc_stream *s;
    fm1_uvc_status status;

    fm1_uvc_setup_init();
    CHECK(fm1_uvc_desc_config(0,descriptors,&itf)==159 && itf==4);
    CHECK(handlers[2] && handlers[3] && reset_handler);
    CHECK(!fm1_uvc_capture_enabled());
    device.bDeviceStates=USB_DEFAULT;
    r=request(0,9,1,0,0);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL);
    r=request(0xa1,FM1_UVC_GET_DEF,0x0100,3,26);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL);

    configure(1);alternate(1,0); /* A commit is required before activation. */
    r=request(0x81,0,0,2,2);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_STAGE_IN &&
          in_length==2 && in_bytes[0]==0 && in_bytes[1]==0);
    vc_error_queries();
    malformed_out_queries();
    commit();alternate(1,1);
    CHECK(fm1_uvc_capture_enabled() && max_packet==962);
    CHECK(packet_length==0 && (txcsr&1u) && arm_count==1 && sync_count==1);
    vc_error_queries(); /* Valid/malformed queries cannot restart live video. */
    malformed_out_queries(); /* Invalid OUT cannot overwrite DMA or the commit. */
    s=fm1_uvc_stream_get();
    (void)queue_rows(0x5a);
    memcpy(previous,dma,sizeof(previous));before=arm_count;
    completion(&device,4); /* An early/spurious callback cannot overwrite DMA. */
    CHECK(arm_count==before && !memcmp(previous,dma,sizeof(previous)));
    CHECK(s->stats.packets_sent==0);
    txcsr|=BIT(7)|BIT(2);
    completion(&device,4); /* Incomplete ISO re-arms owned storage, no new copy. */
    CHECK(arm_count==before+1 && !memcmp(previous,dma,sizeof(previous)) &&
          !(txcsr&(BIT(7)|BIT(2))) && (txcsr&1u) && s->stats.packets_sent==0);
    CHECK(s->stats.retries==1);
    before=arm_count;
    complete_transaction();
    CHECK(packet_length==962 && s->stats.packets_sent==1 && arm_count==before+1);
    fm1_uvc_test_irq_mask=0;
    CHECK(fm1_uvc_get_status(&status)==1 && status.active && status.configured &&
          status.committed && status.alt==1 && status.packets_sent==1);
    CHECK(fm1_uvc_test_irq_mask==0 && fm1_uvc_get_status(NULL)==0);
    CHECK(dma[0]==2 && (dma[1]&FM1_UVC_HEADER_EOH) && dma[2]==0x5a && dma[961]==0x5a);
    memcpy(previous,dma,sizeof(previous));before=arm_count;
    completion(&device,4);
    CHECK(arm_count==before && !memcmp(previous,dma,sizeof(previous)));
    complete_transaction(); /* No data still arms a successful empty packet. */
    CHECK(packet_length==0 && (txcsr&1u) && arm_count==before+1);

    CHECK(fm1_uvc_reserve_rows(s,clock_ms,2,&reservation)==FM1_UVC_OK);
    alternate(0,1);
    CHECK(!fm1_uvc_capture_enabled() && !completion && !endpoint_enabled && !interrupt_enabled);
    CHECK(!(txcsr&1u) && packet_length==0);
    CHECK(fm1_uvc_publish_rows(s,clock_ms,&reservation)==FM1_UVC_STALE);
    alternate(1,1);
    reset_handler(&device,UINT32_MAX);
    CHECK(!fm1_uvc_capture_enabled() && !completion && !endpoint_enabled);
    alternate(1,0);

    configure(1);commit();alternate(1,1);
    configure(0);
    CHECK(!fm1_uvc_capture_enabled() && !completion && !endpoint_enabled);
    alternate(1,0);

    configure(1);
    r=request(0x21,FM1_UVC_SET_CUR,0x0200,3,26);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_STAGE_OUT);
    reads=read_count;before=hardware_stalls;count0=25;
    CHECK(receive(&device,&r)==USB_EP0_SET_STALL && read_count==reads);
    CHECK(hardware_stalls==before+1);
    read_error(FM1_UVC_ERROR_INVALID_REQUEST);
    r=request(0x21,FM1_UVC_SET_CUR,0x0200,3,26);
    CHECK(setup(&r)==1);
    {struct usb_ctrlrequest other=request(0xa1,0x21,0,0,7);CHECK(setup(&other)==0);}
    count0=26;
    before=hardware_stalls;
    CHECK(receive(&device,&r)==USB_EP0_SET_STALL); /* A CDC SETUP canceled OUT. */
    CHECK(hardware_stalls==before+1);

    commit();alternate(1,1);
    fm1_uvc_test_irq_mask=0;reentry_probe=1;fm1_uvc_capture_stop();reentry_probe=0;
    CHECK(fm1_uvc_test_irq_mask==0 && !fm1_uvc_capture_enabled());
    CHECK(irq_nesting==0 && reentry_opportunities==0);
    before=arm_count;
    configure(1);
    r=request(0xa1,FM1_UVC_GET_DEF,0x0100,3,26);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_STAGE_IN);
    r=request(0x21,FM1_UVC_SET_CUR,0x0200,3,26);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL);
    alternate(1,0);
    CHECK(arm_count==before && !completion && !endpoint_enabled);
    CHECK(fm1_uvc_get_status(&status)==1 && status.shutdown && !status.active);
    reset_handler(&device,UINT32_MAX);configure(1);alternate(1,0);
    CHECK(arm_count==before && !completion);
    /* Simulate a new boot only after all reservations and DMA have stopped. */
    fm1_uvc_setup_init();
    configure(1);commit();alternate(1,1);
    fm1_uvc_test_irq_mask=1;before=disable_count;fm1_uvc_capture_stop();
    CHECK(fm1_uvc_test_irq_mask==1 && disable_count==before+1 && !completion);
    alternate(1,0); /* Shutdown/UBOOT stop invalidates configuration and commit. */

    r=request(0,9,2,0,0);
    CHECK(setup(&r)==1 && device.bsetup_phase==USB_EP0_SET_STALL);
    puts("UVC adapter: DMA ownership, empty ISO, alt/reset/config/stop and EP0 lifetime passed");
    return 0;
}
