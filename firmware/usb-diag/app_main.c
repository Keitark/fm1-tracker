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
#ifdef FM1_PERIPHERAL_TESTS
#include "peripherals.h"
#endif

const struct irq_info irq_info_table[]={{-1,-1,-1}};
const struct task_info task_info_table[]={
    {"app_core",15,4096,1024},{"sys_event",29,512,0},
    {"systimer",14,256,0},{"sys_timer",9,512,128},
    {"usb_diag",10,2048,0},
#ifdef FM1_PERIPHERAL_TESTS
#if defined(FM1_NES_PLAYER) || defined(FM1_MDX_PLAYER) || defined(FM1_MOD_PLAYER)
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
        ++fm1_usb_heartbeat;
        if(generation!=fm1_cdc_generation || !fm1_cdc_ready(FM1_USB_CONTROLLER)) {
#ifdef FM1_PERIPHERAL_TESTS
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
#ifdef FM1_PERIPHERAL_TESTS
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
                if(fm1_usb_boot_pending() || fm1_peripheral_request(test,generation))reply(NULL,"ERR TEST_BUSY\n");
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
#ifdef FM1_MOD_PLAYER
                         "profile=mod-editor/1 "
#elif defined(FM1_MDX_PLAYER)
                         "profile=mdx-karaoke/1 "
#else
                         "profile=peripheral-test/1 "
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
            if(n)tx_r+=cdc_write_data(FM1_USB_CONTROLLER,out,n);
        }
        os_time_dly(1);
    }
}
void app_main(void) {
    fm1_boot_trace_mark(FM1_TRACE_APP); fm1_usb_stage=1;
#ifdef FM1_PERIPHERAL_TESTS
    /* A failed optional worker must not remove serial recovery. TEST STATUS
     * exposes ready=0 and test starts are rejected if task creation fails. */
    (void)fm1_peripheral_start_task();
#endif
    fm1_usb_error=task_create(fm1_usb_task,NULL,"usb_diag");
    if(fm1_usb_error)fm1_usb_stage=0xff;
}
