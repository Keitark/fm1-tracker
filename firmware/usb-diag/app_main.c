/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Keitark
 * See firmware/usb-audio/RELICENSE.md for the scoped license grant. */
#include "app_config.h"
#include "system/includes.h"
#include "system/task.h"
#include "os/os_api.h"
#include "system/sys_time.h"
#include "usb/device/cdc.h"
#include "usb/usb_config.h"
#include "boot_trace.h"
#include "protocol.h"
#include "boot_entry.h"
#ifdef FM1_USB_MIDI_UPDATE
#include "fm1_midi_target.h"
#include "fm1_midi_update.h"
#endif
#ifdef FM1_USB_AUDIO
#include "target.h"
#endif
#ifdef FM1_USB_VIDEO
#include "fm1_uvc.h"
#endif
#ifdef FM1_FORGE
#include "forge_target.h"
#endif
#ifdef FM1_PERIPHERAL_TESTS
#include "peripherals.h"
#endif

const struct irq_info irq_info_table[]={{-1,-1,-1}};
const struct task_info task_info_table[]={
    {"app_core",15,4096,1024},{"sys_event",29,512,0},
    {"systimer",14,256,0},{"sys_timer",9,512,128},
#ifdef FM1_USB_MIDI_UPDATE
    {"#C0update",9,2048,0},
#endif
#ifdef FM1_FORGE
    {"#C0usb_diag",10,2048,0},
#else
    {"usb_diag",10,2048,0},
#endif
#ifdef FM1_PERIPHERAL_TESTS
#ifdef FM1_FORGE
    {"#C0peripheral",8,4096,0},
#elif defined(FM1_NES_PLAYER) || defined(FM1_MDX_PLAYER) || defined(FM1_MOD_PLAYER)
    {"peripheral",8,4096,0},
#else
    {"peripheral",8,2048,0},
#endif
#endif
    {0,0,0,0,0}
};
volatile uint32_t fm1_usb_stage, fm1_usb_heartbeat, fm1_usb_tx_dropped;
volatile int fm1_usb_error;
static fm1_diag_protocol protocol;
static char tx[1024];
static unsigned tx_r,tx_w;
extern int fm1_cdc_ready(usb_dev id);
extern volatile unsigned fm1_cdc_generation;
#ifdef FM1_FORGE
extern u32 fm1_cdc_write_packet(usb_dev id,u8 *data,u32 size,unsigned generation);
#endif

static void reply(void *ctx,const char *s) {
    unsigned n=strlen(s),i; (void)ctx;
    if(n>sizeof(tx)-(tx_w-tx_r)) { ++fm1_usb_tx_dropped; return; }
    for(i=0;i<n;i++)tx[(tx_w++)%sizeof(tx)]=s[i];
}
__attribute__((noinline,used))
static void fm1_usb_task(void *arg) {
    uint8_t rx[64],out[63]; unsigned generation=0,i,n; uint32_t last=0;
    (void)arg; fm1_boot_trace_mark(FM1_TRACE_WORKER); fm1_usb_stage=2;
    /* Explicit single-controller candidate. No host/OTG detection or VBUS drive. */
    fm1_usb_error=usb_device_mode(FM1_USB_CONTROLLER,CDC_CLASS);
    if(fm1_usb_error) { fm1_usb_stage=0xff; for(;;)os_time_dly(100); }
    fm1_usb_stage=3;
    for(;;) {
        uint32_t now=timer_get_ms();
#ifdef FM1_USB_MIDI_UPDATE
        fm1_usb_midi_poll(now);
#endif
        ++fm1_usb_heartbeat;
        if(generation!=fm1_cdc_generation || !fm1_cdc_ready(FM1_USB_CONTROLLER)) {
#if defined(FM1_PERIPHERAL_TESTS) && !defined(FM1_FORGE)
            fm1_peripheral_session_cancel();
#endif
            generation=fm1_cdc_generation; fm1_diag_reset(&protocol); tx_r=tx_w=0; last=now-1000;
        }
        if(fm1_cdc_ready(FM1_USB_CONTROLLER)) {
            if(fm1_usb_rx_fault()) {
                fm1_diag_reset(&protocol);tx_r=tx_w=0;
#ifdef FM1_PERIPHERAL_TESTS
                fm1_peripheral_cancel();
#endif
                reply(NULL,"ERR RX_OR_UBOOT_ABORTED\n");
            }
            n=cdc_read_data(FM1_USB_CONTROLLER,rx,sizeof(rx));
            if(generation!=fm1_usb_rx_generation()) {
                generation=fm1_usb_rx_generation();fm1_diag_reset(&protocol);tx_r=tx_w=0;
#if defined(FM1_PERIPHERAL_TESTS) && !defined(FM1_FORGE)
                fm1_peripheral_session_cancel();
#endif
            }
            if(n)fm1_diag_feed(&protocol,rx,n,now,reply,NULL);
            if(protocol.boot_requested) {
                fm1_diag_reset(&protocol);
#ifdef FM1_PERIPHERAL_TESTS
                fm1_peripheral_cancel();
                if(!fm1_peripheral_idle())reply(NULL,"ERR UBOOT_RETRY_AFTER_TEST_END\n");
                else
#endif
                if(tx_w==tx_r && fm1_usb_boot_arm())reply(NULL,"OK UBOOT ARMED CONFIRM-WITHIN-5000MS\n");
                else reply(NULL,"ERR UBOOT_RETRY\n");
            }
#ifdef FM1_PERIPHERAL_TESTS
            if(protocol.test_requested) {
                unsigned test=protocol.test_requested;protocol.test_requested=0;
                if(fm1_usb_boot_pending() ||
#ifdef FM1_USB_MIDI_UPDATE
                   /* STOP and STATUS remain available; new test workers
                    * cannot reclaim peripherals during an update session. */
                   (fm1_midi_update_busy() && test!=1 && test!=6) ||
#endif
                   fm1_peripheral_request(test,generation))reply(NULL,"ERR TEST_BUSY\n");
                else reply(NULL,"OK TEST REQUEST\n");
            }
            if(!fm1_usb_boot_pending() && tx_w==tx_r) {
                char event[128];if(fm1_peripheral_event(event,sizeof(event)))reply(NULL,event);
            }
#endif
            fm1_diag_tick(&protocol,now,reply,NULL);
            if(!fm1_usb_boot_pending() && !protocol.active && !protocol.used && (uint32_t)(now-last)>=1000 && tx_w==tx_r) {
                char s[192]; last=now;
                snprintf(s,sizeof(s),"# FM1DIAG/1 "
#ifdef FM1_PERIPHERAL_TESTS
#ifdef FM1_FORGE
                         "profile=fm1-forge/1 "
#else
#ifdef FM1_MOD_PLAYER
                         "profile=mod-editor/1 "
#elif defined(FM1_MDX_PLAYER)
                         "profile=mdx-karaoke/1 "
#else
                         "profile=peripheral-test/1 "
#endif
#endif
#endif
                         "stage=%lu trace=%08lx uptime=%lu boot=serial-uboot update=verify-only commit=blocked\n",
                         (unsigned long)fm1_usb_stage,(unsigned long)fm1_boot_trace.visited,(unsigned long)now);
                reply(NULL,s);
            }
            /* Short packets avoid an extra potentially blocking ZLP write.
             * One owner/task for CDC data mutex; ISR never transmits. */
            n=tx_w-tx_r; if(n>sizeof(out))n=sizeof(out);
            for(i=0;i<n;i++)out[i]=tx[(tx_r+i)%sizeof(tx)];
#ifdef FM1_FORGE
            if(n)tx_r+=fm1_cdc_write_packet(FM1_USB_CONTROLLER,out,n,generation);
#else
            if(n)tx_r+=cdc_write_data(FM1_USB_CONTROLLER,out,n);
#endif
        }
        os_time_dly(1);
    }
}
void app_main(void) {
    fm1_boot_trace_mark(FM1_TRACE_APP); fm1_usb_stage=1;
#ifdef FM1_USB_MIDI_UPDATE
    fm1_usb_midi_init();
#endif
#ifdef FM1_FORGE
    fm1_forge_clock_prepare();
#endif
#ifdef FM1_USB_AUDIO
    fm1_usb_audio_init();
#endif
#ifdef FM1_USB_VIDEO
    fm1_uvc_setup_init(); /* Exactly once, before worker and USB attachment. */
#endif
#ifdef FM1_PERIPHERAL_TESTS
    /* A failed optional worker must not remove serial recovery. TEST STATUS
     * exposes ready=0 and test starts are rejected if task creation fails. */
    (void)fm1_peripheral_start_task();
#endif
    fm1_usb_error=task_create(fm1_usb_task,NULL,"usb_diag");
    if(fm1_usb_error)fm1_usb_stage=0xff;
}
