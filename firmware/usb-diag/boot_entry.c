#include "app_config.h"
#include "system/includes.h"
#include "system/sys_time.h"
#include "usb/device/usb_stack.h"
#include "rx_channel.h"
#include "boot_entry.h"

extern int fm1_cdc_ready(usb_dev id);
extern volatile unsigned fm1_cdc_generation;
extern void go_mask_usb_updata(void);
static fm1_rx_channel channel;
static unsigned consumed_generation;

static void sync_channel(void) {
    fm1_rx_sync(&channel,fm1_cdc_generation,
                fm1_cdc_ready(FM1_USB_CONTROLLER),timer_get_ms());
}
/* Only caller is the CDC bulk OUT interrupt callback. No mutex, logging,
 * allocation, task dispatch, or serial transmission in this path.
 * TODO_HW_VERIFY: WL82 CDC -> mask/UBOOT enumeration on the FM-1 connector.
 * KALLSUP establishes the IRQ-context pattern, not WL82 hardware acceptance.
 */
__attribute__((noinline,used))
void fm1_usb_rx_irq(struct usb_device_t *device) {
    uint8_t bytes[MAXP_SIZE_CDC_BULKOUT];
    usb_dev id=usb_device2id(device);
    unsigned n;
    if(id!=FM1_USB_CONTROLLER)return;
    sync_channel();
    n=usb_g_bulk_read(id,CDC_DATA_EP_OUT,bytes,sizeof(bytes),0);
    if(!fm1_cdc_ready(id))return;
    if(n>sizeof(bytes)) {channel.fault=1;channel.armed=0;return;}
    if(fm1_rx_receive(&channel,bytes,n,timer_get_ms())) {
        go_mask_usb_updata();
        for(;;) {} /* SDK entry must not return to the running application. */
    }
}
unsigned fm1_usb_rx_take(uint8_t *out, unsigned limit) {
    unsigned n;
    local_irq_disable(); sync_channel(); n=fm1_rx_take(&channel,out,limit);
    consumed_generation=channel.generation; local_irq_enable();
    return n;
}
int fm1_usb_boot_arm(void) {
    int ok;
    local_irq_disable(); sync_channel();
    ok=consumed_generation==channel.generation && fm1_cdc_ready(FM1_USB_CONTROLLER) &&
       fm1_rx_arm(&channel,timer_get_ms());
    local_irq_enable(); return ok;
}
int fm1_usb_rx_fault(void) {
    int fault;
    local_irq_disable(); sync_channel(); fault=channel.fault;
    if(fault)fm1_rx_reset(&channel,fm1_cdc_generation);
    local_irq_enable(); return fault;
}
int fm1_usb_boot_pending(void) {
    int pending;
    local_irq_disable(); sync_channel(); pending=channel.armed; local_irq_enable();
    return pending;
}
unsigned fm1_usb_rx_generation(void) {return consumed_generation;}
