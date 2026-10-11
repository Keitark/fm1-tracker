#include "hs_phy_probe.h"

#ifndef FM1_HS_PHY_TEST
#include "system/includes.h"
#include "system/sys_time.h"
#include "os/os_api.h"
#include "asm/hwi.h"
typedef struct fm1_hs_phy_ops {
    void *ctx;
    uint8_t (*read8)(void *, uint32_t);
    uint16_t (*read16)(void *, uint32_t);
    uint32_t (*read32)(void *, uint32_t);
    void (*write8)(void *, uint32_t, uint8_t);
    void (*write16)(void *, uint32_t, uint16_t);
    void (*write32)(void *, uint32_t, uint32_t);
    void (*disable_irq)(void *, unsigned);
    void (*barrier)(void *);
    void (*delay)(void *, unsigned);
    uint32_t (*now_ms)(void *);
    void (*sleep_tick)(void *);
} fm1_hs_phy_ops;
#endif

#ifdef FM1_HS_PHY_TEST
#define HS_RETAIN
#else
#define HS_RETAIN __attribute__((noinline, used))
#endif

/* WL82 HUSB0 is the SDK's USB1 controller. Addresses and write order match
 * pinned cpu.a(usb.c.o), usb_g_sie_init(1), usb_slave_init(1), usb_read_intr(1)
 * and usb_g_hold(1). No generic USB1 configuration or endpoint path is used. */
#define H0_POWER       0x16001u
#define H0_INTRTX      0x16002u
#define H0_INTRRX      0x16004u
#define H0_INTRTXE     0x16006u
#define H0_INTRRXE     0x16008u
#define H0_INTRUSB     0x1600au
#define H0_INTRUSBE    0x1600bu
#define H0_RX_DPKTDIS  0x16340u
#define H0_TX_DPKTDIS  0x16342u
#define H0_SIE_CON     0x16800u
#define HUSB_PHY0_CON0 0x16a0cu
#define HS_BIT(n) (1u << (n))
#define READY_READ_LIMIT 20000u
#define NEGOTIATION_MS 5000u
/* The pinned SDK tick is 10 ms. This second cap prevents a stalled millisecond
 * clock from turning the probe into an unbounded loop (500 sleeps maximum). */
#define SAMPLE_LIMIT 500u

static fm1_hs_phy_report report;

static void update32(const fm1_hs_phy_ops *ops, uint32_t address,
                     uint32_t clear, uint32_t set)
{
    uint32_t value = ops->read32(ops->ctx, address);
    ops->write32(ops->ctx, address, (value & ~clear) | set);
}

static void mask_interrupts(const fm1_hs_phy_ops *ops)
{
    ops->disable_irq(ops->ctx, 53);
    ops->disable_irq(ops->ctx, 54);
    ops->write8(ops->ctx, H0_INTRUSBE, 0);
    ops->write16(ops->ctx, H0_INTRTXE, 0);
    ops->write16(ops->ctx, H0_INTRRXE, 0);
}

static uint8_t take_interrupts(const fm1_hs_phy_ops *ops)
{
    uint8_t usb;
    /* SDK raw interrupt read gates BIT12 around all three status registers.
     * Reading acknowledges the snapshot; no ISR or EP0 dispatch is installed. */
    update32(ops, H0_SIE_CON, HS_BIT(12), 0);
    ops->barrier(ops->ctx);
    usb = ops->read8(ops->ctx, H0_INTRUSB);
    (void)ops->read16(ops->ctx, H0_INTRTX);
    (void)ops->read16(ops->ctx, H0_INTRRX);
    update32(ops, H0_SIE_CON, 0, HS_BIT(12));
    return usb;
}

static void hold(const fm1_hs_phy_ops *ops)
{
    ops->write8(ops->ctx, H0_POWER, 0);
    mask_interrupts(ops);
    update32(ops, H0_SIE_CON, HS_BIT(0) | HS_BIT(1), 0);
    update32(ops, HUSB_PHY0_CON0, HS_BIT(0) | HS_BIT(1), 0);
    ops->barrier(ops->ctx);
}

static void probe(const fm1_hs_phy_ops *ops)
{
    uint32_t started, i;
    report = (fm1_hs_phy_report){0};
    /* CPU USB1 vectors stay disabled even if PHY initialization or host reset
     * restores controller masks. USB0 has not been initialized at this point. */
    mask_interrupts(ops);
    ops->write8(ops->ctx, H0_POWER, 0);
    ops->write32(ops->ctx, H0_SIE_CON, 0);
    update32(ops, HUSB_PHY0_CON0, HS_BIT(5), 0);
    update32(ops, HUSB_PHY0_CON0, HS_BIT(8) | HS_BIT(9), 0);
    update32(ops, HUSB_PHY0_CON0, 0, HS_BIT(2) | HS_BIT(3) | HS_BIT(4));
    update32(ops, HUSB_PHY0_CON0, 0, HS_BIT(0));
    ops->delay(ops->ctx, 1000); /* SDK finite NOP/decrement delay. */
    update32(ops, HUSB_PHY0_CON0, 0, HS_BIT(1));
    update32(ops, H0_SIE_CON, 0, HS_BIT(0) | HS_BIT(1));
    for (i = 0; i < READY_READ_LIMIT; ++i) {
        ++report.polls;
        if (ops->read32(ops->ctx, H0_SIE_CON) & HS_BIT(4)) {
            report.ready = 1;
            break;
        }
    }
    if (!report.ready) {
        report.status = FM1_HS_PHY_READY_TIMEOUT;
        hold(ops);
        return;
    }
    update32(ops, HUSB_PHY0_CON0, HS_BIT(25), 0);
    update32(ops, HUSB_PHY0_CON0, HS_BIT(24), 0);
    update32(ops, HUSB_PHY0_CON0, 0, HS_BIT(22));
    update32(ops, HUSB_PHY0_CON0, 0, HS_BIT(23));
    update32(ops, H0_SIE_CON, 0, HS_BIT(11) | HS_BIT(12));
    ops->write16(ops->ctx, H0_TX_DPKTDIS, 0xffffu);
    ops->write16(ops->ctx, H0_RX_DPKTDIS, 0xffffu);
    /* usb_slave_init(1), with explicit masks and no allocated USB1 context. */
    mask_interrupts(ops);
    update32(ops, H0_SIE_CON, 0, HS_BIT(1));
    (void)take_interrupts(ops); /* Discard any stale pre-attachment reset. */
    started = ops->now_ms(ops->ctx);
    ops->write8(ops->ctx, H0_POWER, 0x60); /* HSENA | SOFTCONN. */
    for (i = 0; i < SAMPLE_LIMIT; ++i) {
        uint8_t intr;
        report.elapsed_ms = ops->now_ms(ops->ctx) - started;
        if (report.elapsed_ms >= NEGOTIATION_MS)
            break;
        intr = take_interrupts(ops);
        ++report.samples;
        if (intr & HS_BIT(2)) {
            report.reset_seen = 1;
            mask_interrupts(ops);
        }
        report.power = ops->read8(ops->ctx, H0_POWER);
        if (report.reset_seen && (report.power & HS_BIT(4)))
            report.high_seen = 1;
        ops->sleep_tick(ops->ctx);
    }
    report.elapsed_ms = ops->now_ms(ops->ctx) - started;
    report.status = !report.reset_seen ? FM1_HS_PHY_NO_RESET :
        report.high_seen ? FM1_HS_PHY_HIGH_SPEED : FM1_HS_PHY_RESET_NO_HS;
    hold(ops);
}

HS_RETAIN const fm1_hs_phy_report *fm1_hs_phy_probe_report(void)
{
    return &report;
}

#ifdef FM1_HS_PHY_TEST
void fm1_hs_phy_probe_run_with_ops(const fm1_hs_phy_ops *ops)
{
    probe(ops);
}
#else
static uint8_t hw_read8(void *ctx, uint32_t address)
{ (void)ctx; return *(volatile uint8_t *)(uintptr_t)address; }
static uint16_t hw_read16(void *ctx, uint32_t address)
{ (void)ctx; return *(volatile uint16_t *)(uintptr_t)address; }
static uint32_t hw_read32(void *ctx, uint32_t address)
{ (void)ctx; return *(volatile uint32_t *)(uintptr_t)address; }
static void hw_write8(void *ctx, uint32_t address, uint8_t value)
{ (void)ctx; *(volatile uint8_t *)(uintptr_t)address = value; }
static void hw_write16(void *ctx, uint32_t address, uint16_t value)
{ (void)ctx; *(volatile uint16_t *)(uintptr_t)address = value; }
static void hw_write32(void *ctx, uint32_t address, uint32_t value)
{ (void)ctx; *(volatile uint32_t *)(uintptr_t)address = value; }
static void hw_disable_irq(void *ctx, unsigned vector)
{ (void)ctx; irq_disable(vector); }
static void hw_barrier(void *ctx)
{ (void)ctx; __asm__ volatile("csync" ::: "memory"); }
static void hw_delay(void *ctx, unsigned n)
{ (void)ctx; delay(n); }
static uint32_t hw_now_ms(void *ctx)
{ (void)ctx; return timer_get_ms(); }
static void hw_sleep_tick(void *ctx)
{ (void)ctx; wdt_clear(); os_time_dly(1); }
HS_RETAIN void fm1_hs_phy_probe_run(void)
{
    static int ran;
    static const fm1_hs_phy_ops ops = {
        0, hw_read8, hw_read16, hw_read32, hw_write8, hw_write16,
        hw_write32, hw_disable_irq, hw_barrier, hw_delay, hw_now_ms,
        hw_sleep_tick
    };
    if (!ran) {
        ran = 1;
        probe(&ops);
    }
}
#endif
