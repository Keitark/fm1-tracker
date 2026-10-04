#include "wl82_services.h"
#include "clock_contract.h"
#include "fm1_wl82_keyscan.h"
#include <string.h>
#ifdef FM1_SDK_SERVICES_TEST
#include "fake_sdk_services.h"
#else
#include "asm/cpu.h"
#include "asm/clock.h"
#include "asm/wdt.h"
#include "system/spinlock.h"
#include "system/sys_time.h"
#include "os/os_api.h"
#endif

/* Queue and dispatcher have separate locks. Never stop the IRQ while holding
   the queue lock: the running callback can need it. Local masking prevents
   same-core re-entry; testset + csync serializes the two physical cores. */
static spinlock_t queue_lock,dispatch_lock;
static fm1_wl82_keyscan scanner;
static uint64_t keys;
static void (*audio_handler)(void);
static int prepared,irq_registered;
volatile fm1_sdk_diagnostics fm1_sdk_diag;

#ifdef FM1_SDK_SERVICES_TEST
#define COMPILER_BARRIER() fm1_fake_barrier()
#else
#define COMPILER_BARRIER() __asm__ volatile("" ::: "memory")
#endif

static unsigned take(spinlock_t *lock) {
    unsigned flags;
    local_irq_save(flags);COMPILER_BARRIER();
    arch_spin_lock(lock);COMPILER_BARRIER();
    return flags;
}
static void release(spinlock_t *lock,unsigned flags) {
    COMPILER_BARRIER();arch_spin_unlock(lock);COMPILER_BARRIER();
    local_irq_restore(flags);COMPILER_BARRIER();
}
static unsigned queue_save(void *c) {(void)c;return take(&queue_lock);}
static void queue_restore(void *c,unsigned flags) {(void)c;release(&queue_lock,flags);}

/* Microsecond units, millisecond resolution. Multiplication wraps modulo 2^32
   consistently even when the SDK's underlying millisecond counter wraps. */
static uint32_t now_us(void *c) {(void)c;return timer_get_ms()*1000u;}
static int wait_us(void *c,uint32_t duration) {
    uint32_t start=now_us(c),iterations=0;
    if(!duration)return 0;
    if(duration>0x7fffffffu-1000u)return FM1_NES_INVALID;
    /* Add one quantization interval to guarantee at least the requested wait.
       RTOS tick granularity can make this longer; IRQs remain enabled. */
    while((uint32_t)(now_us(c)-start)<duration+1000u) {
        wdt_clear();
        if(fm1_sdk_diag.fault)return FM1_NES_IO_ERROR;
        os_time_dly(1);
        /* A stopped clock must not hang before the first LCD command. One
           yield is at least a tick; this is deliberately a loose upper bound. */
        if(++iterations>duration/1000u+1024u) {
            fm1_sdk_diag.fault=FM1_NES_IO_ERROR;
            return FM1_NES_IO_ERROR;
        }
    }
    return 0;
}
static uint64_t read_keys(void *c) {
    int rc;(void)c;
    if(!prepared || fm1_sdk_diag.fault)return 0;
    rc=fm1_wl82_keyscan_poll(&scanner,&keys);
    if(rc){fm1_sdk_diag.fault=rc;keys=0;}
    else ++fm1_sdk_diag.scans;
    wdt_clear();return keys;
}
static int stop_requested(void *c) {
    (void)c;wdt_clear();return fm1_sdk_diag.fault!=0;
}

/* An ABI ISR, not an ordinary C function registered as one. Handler execution
   is enclosed by dispatch_lock so teardown cannot free live SDK DMA state. */
___interrupt
static void fm1_alink_isr(void) {
    unsigned flags=take(&dispatch_lock);
    if(audio_handler){audio_handler();++fm1_sdk_diag.audio_irqs;}
    release(&dispatch_lock,flags);
}
static int audio_start(void *c,void (*handler)(void)) {
    unsigned flags;(void)c;
    if(!prepared || !handler)return FM1_NES_INVALID;
    if(irq_registered)return FM1_NES_BUSY;
    flags=take(&dispatch_lock);audio_handler=handler;
    release(&dispatch_lock,flags);
    /* Stock uses ALINK0 IRQ 11, priority 3, CPU 0. */
    request_irq(IRQ_ALNK_IDX,3,fm1_alink_isr,0);irq_registered=1;
    return 0;
}
static void audio_stop(void *c) {
    unsigned flags;(void)c;
    if(!irq_registered)return;
    bit_clr_ie(IRQ_ALNK_IDX,0);
    /* If CPU0 is inside the handler, acquiring this lock waits for it. An
       already-entered wrapper arriving later sees NULL and does no SDK I/O. */
    flags=take(&dispatch_lock);audio_handler=0;
    release(&dispatch_lock,flags);
    unrequest_irq(IRQ_ALNK_IDX,0);irq_registered=0;
}
static int prepare(void *c) {
    uint32_t before;int rc,sys,lsb;(void)c;
    if(prepared || irq_registered)return FM1_NES_BUSY;
    keys=0;fm1_sdk_diag.fault=0;fm1_sdk_diag.scans=0;fm1_sdk_diag.audio_irqs=0;
    sys=clk_get("sys");lsb=clk_get("lsb");
    fm1_sdk_diag.sys_hz=(uint32_t)sys;fm1_sdk_diag.lsb_hz=(uint32_t)lsb;
    if(!fm1_clock_report_valid(sys,lsb)) {
        fm1_sdk_diag.fault=FM1_NES_BSP_UNVERIFIED;
        return fm1_sdk_diag.fault;
    }
    before=timer_get_ms();wdt_clear();os_time_dly(1);
    if(timer_get_ms()==before){fm1_sdk_diag.fault=FM1_NES_IO_ERROR;return fm1_sdk_diag.fault;}
    rc=fm1_wl82_keyscan_start(&scanner,0,now_us);
    if(rc){fm1_sdk_diag.fault=rc;return rc;}
    prepared=1;return 0;
}
static void finish(void *c) {
    audio_stop(c);fm1_wl82_keyscan_stop(&scanner);prepared=0;keys=0;
}
int fm1_sdk_services_init(fm1_wl82_services *s) {
    if(!s)return FM1_NES_INVALID;
    if(prepared || irq_registered)return FM1_NES_BUSY;
    memset(s,0,sizeof(*s));
    s->prepare=prepare;s->finish=finish;s->now_us=now_us;s->wait_us=wait_us;
    s->read_keys=read_keys;s->stop_requested=stop_requested;
    s->audio_irq_start=audio_start;s->audio_irq_stop=audio_stop;
    s->irq_save=queue_save;s->irq_restore=queue_restore;
    s->mute_policy=FM1_AUDIO_MUTE_DIGITAL_ONLY;
    return 0;
}
