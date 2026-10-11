#include "hs_phy_probe.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define POWER 0x16001u
#define INTRTX 0x16002u
#define INTRRX 0x16004u
#define INTRTXE 0x16006u
#define INTRRXE 0x16008u
#define INTRUSB 0x1600au
#define INTRUSBE 0x1600bu
#define RX_DPKTDIS 0x16340u
#define TX_DPKTDIS 0x16342u
#define SIE 0x16800u
#define PHY 0x16a0cu
#define NEVER UINT32_MAX

typedef struct event {
    char kind;
    uint32_t address, value;
} event;
typedef struct fake {
    uint8_t power, usbe;
    uint16_t txe, rxe, rxdisable, txdisable;
    uint32_t sie, phy, now, attached_at, sleep_step;
    uint32_t ready_after, ready_reads, reset_at, high_at;
    unsigned irq53, irq54, slept, attached, reset_delivered;
    unsigned delay_count, delay_units, stale_reset, frozen_clock;
    unsigned log_count, barriers, irq_snapshot_stage;
    event log[6000];
} fake;

static void record(fake *f, char kind, uint32_t address, uint32_t value)
{
    assert(f->log_count < sizeof(f->log) / sizeof(f->log[0]));
    f->log[f->log_count++] = (event){kind, address, value};
}
static uint8_t read8(void *ctx, uint32_t address)
{
    fake *f = ctx;
    assert(f->irq53 && f->irq54);
    assert(address == POWER || address == INTRUSB);
    record(f, 'b', address, 0);
    if (address == POWER) {
        uint32_t age = f->now - f->attached_at;
        assert(f->irq_snapshot_stage == 0);
        return (uint8_t)(f->power |
            (f->attached && f->high_at != NEVER && age >= f->high_at ? 0x10 : 0));
    }
    assert(!(f->sie & 0x1000));
    assert(f->irq_snapshot_stage == 1);
    f->irq_snapshot_stage = 2;
    if (!f->attached && f->stale_reset) {
        f->stale_reset = 0;
        return 4;
    }
    if (f->attached && !f->reset_delivered && f->reset_at != NEVER &&
        (uint32_t)(f->now - f->attached_at) >= f->reset_at) {
        f->reset_delivered = 1;
        /* The controller can restore default masks on host reset. */
        f->usbe = 0xff;
        f->txe = f->rxe = 0xffff;
        return 4;
    }
    return 0;
}
static uint16_t read16(void *ctx, uint32_t address)
{
    fake *f = ctx;
    assert(f->irq53 && f->irq54);
    assert(address == INTRTX || address == INTRRX);
    assert(!(f->sie & 0x1000));
    if (address == INTRTX) {
        assert(f->irq_snapshot_stage == 2);
        f->irq_snapshot_stage = 3;
    } else {
        assert(f->irq_snapshot_stage == 3);
        f->irq_snapshot_stage = 4;
    }
    record(f, 'h', address, 0);
    return 0;
}
static uint32_t read32(void *ctx, uint32_t address)
{
    fake *f = ctx;
    assert(f->irq53 && f->irq54);
    assert(address == SIE || address == PHY);
    if (address == PHY)
        return f->phy;
    if ((f->sie & 3) == 3) {
        ++f->ready_reads;
        if (f->ready_after != NEVER && f->ready_reads >= f->ready_after)
            return f->sie | 0x10;
    }
    return f->sie;
}
static void write8(void *ctx, uint32_t address, uint8_t value)
{
    fake *f = ctx;
    assert(f->irq53 && f->irq54);
    assert(address == POWER || address == INTRUSBE);
    record(f, 'B', address, value);
    if (address == INTRUSBE) {
        assert(value == 0);
        f->usbe = value;
    } else {
        assert(value == 0 || value == 0x60);
        if (value == 0x60) {
            assert(f->rxdisable == 0xffff && f->txdisable == 0xffff);
            assert(f->usbe == 0 && f->txe == 0 && f->rxe == 0);
            assert(f->irq_snapshot_stage == 0);
            f->attached_at = f->now;
            f->attached = 1;
        } else {
            f->attached = 0;
        }
        f->power = value;
    }
}
static void write16(void *ctx, uint32_t address, uint16_t value)
{
    fake *f = ctx;
    assert(f->irq53 && f->irq54);
    assert(address == INTRTXE || address == INTRRXE ||
           address == RX_DPKTDIS || address == TX_DPKTDIS);
    record(f, 'H', address, value);
    switch (address) {
    case INTRTXE: assert(value == 0); f->txe = value; break;
    case INTRRXE: assert(value == 0); f->rxe = value; break;
    case RX_DPKTDIS: assert(value == 0xffff); f->rxdisable = value; break;
    case TX_DPKTDIS: assert(value == 0xffff); f->txdisable = value; break;
    }
}
static void write32(void *ctx, uint32_t address, uint32_t value)
{
    fake *f = ctx;
    assert(f->irq53 && f->irq54);
    assert(address == SIE || address == PHY);
    record(f, 'W', address, value);
    if (address == PHY) {
        f->phy = value;
    } else {
        if (f->irq_snapshot_stage == 4) {
            assert(value & 0x1000);
            f->irq_snapshot_stage = 0;
        }
        f->sie = value & ~0x10u; /* Ready flag is read-only. */
    }
}
static void disable_irq(void *ctx, unsigned irq)
{
    fake *f = ctx;
    assert(irq == 53 || irq == 54);
    if (irq == 53) f->irq53 = 1;
    else f->irq54 = 1;
    record(f, 'I', irq, 0);
}
static void barrier(void *ctx)
{
    fake *f = ctx;
    ++f->barriers;
    if ((f->sie & 3) == 3 && !(f->sie & 0x1000)) {
        assert(f->irq_snapshot_stage == 0);
        f->irq_snapshot_stage = 1;
    }
    record(f, 'C', 0, 0);
}
static void finite_delay(void *ctx, unsigned units)
{
    fake *f = ctx;
    ++f->delay_count;
    f->delay_units += units;
    assert(units == 1000);
    assert((f->phy & 3) == 1); /* PHY clock enabled, UTM still disabled. */
    record(f, 'D', 0, units);
}
static uint32_t now_ms(void *ctx) { return ((fake *)ctx)->now; }
static void sleep_tick(void *ctx)
{
    fake *f = ctx;
    assert(f->attached && f->irq_snapshot_stage == 0);
    assert(f->usbe == 0 && f->txe == 0 && f->rxe == 0);
    ++f->slept;
    if (!f->frozen_clock) f->now += f->sleep_step;
}
static void init(fake *f)
{
    memset(f, 0, sizeof(*f));
    f->ready_after = 1;
    f->reset_at = f->high_at = NEVER;
    f->sleep_step = 10;
    f->phy = 0x03000320; /* Exercise all required clear operations. */
    f->usbe = 0xaa;
    f->txe = f->rxe = 0x5555;
}
static void run(fake *f)
{
    const fm1_hs_phy_ops ops = {
        f, read8, read16, read32, write8, write16, write32, disable_irq,
        barrier, finite_delay, now_ms, sleep_tick
    };
    fm1_hs_phy_probe_run_with_ops(&ops);
    /* Every exit returns a disconnected, held USB1 with all masks disabled. */
    assert(f->power == 0 && f->attached == 0);
    assert((f->sie & 3) == 0 && (f->phy & 3) == 0);
    assert(f->usbe == 0 && f->txe == 0 && f->rxe == 0);
    assert(f->irq53 && f->irq54 && f->irq_snapshot_stage == 0);
    assert(f->delay_count == 1 && f->delay_units == 1000);
    assert(f->slept <= 500);
}
static void verify_init_order(const fake *f)
{
    /* Ignore status reads and check the SDK's nontrivial PHY write sequence. */
    uint32_t expected[] = {
        0x03000300, 0x03000000, 0x0300001c, 0x0300001d,
        0x0300001f, 0x0100001f, 0x0000001f, 0x0040001f, 0x00c0001f
    };
    unsigned i, n = 0;
    for (i = 0; i < f->log_count && n < sizeof(expected)/sizeof(expected[0]); ++i) {
        if (f->log[i].kind == 'W' && f->log[i].address == PHY)
            assert(f->log[i].value == expected[n++]);
    }
    assert(n == sizeof(expected)/sizeof(expected[0]));
}
int main(void)
{
    static fake f;
    const fm1_hs_phy_report *r;
    assert(fm1_hs_phy_probe_report()->status == FM1_HS_PHY_NOT_RUN);

    init(&f); f.ready_after = NEVER; run(&f); r = fm1_hs_phy_probe_report();
    assert(!r->ready && r->polls == 20000 && !r->samples);
    assert(r->status == FM1_HS_PHY_READY_TIMEOUT && !r->reset_seen && !r->high_seen);
    assert(f.slept == 0 && f.rxdisable == 0 && f.txdisable == 0);

    init(&f); f.ready_after = 3; run(&f); r = fm1_hs_phy_probe_report();
    assert(r->ready && r->polls == 3 && r->samples == 500);
    assert(r->elapsed_ms == 5000 && r->power == 0x60);
    assert(r->status == FM1_HS_PHY_NO_RESET && !r->reset_seen && !r->high_seen);
    verify_init_order(&f);

    init(&f); f.reset_at = 30; f.high_at = 40; run(&f); r = fm1_hs_phy_probe_report();
    assert(r->status == FM1_HS_PHY_HIGH_SPEED && r->reset_seen && r->high_seen);
    assert(r->power == 0x70 && r->elapsed_ms == 5000);

    init(&f); f.reset_at = 30; run(&f); r = fm1_hs_phy_probe_report();
    assert(r->status == FM1_HS_PHY_RESET_NO_HS && r->reset_seen && !r->high_seen);
    assert(r->power == 0x60);

    init(&f); f.stale_reset = 1; f.high_at = 0; run(&f); r = fm1_hs_phy_probe_report();
    assert(r->status == FM1_HS_PHY_NO_RESET && !r->reset_seen && !r->high_seen);
    assert(r->power == 0x70); /* Raw mode bit alone is insufficient evidence. */

    init(&f); f.now = UINT32_MAX - 100; f.reset_at = 30; f.high_at = 40;
    run(&f); r = fm1_hs_phy_probe_report();
    assert(r->elapsed_ms == 5000 && r->status == FM1_HS_PHY_HIGH_SPEED);

    init(&f); f.sleep_step = 10000; run(&f); r = fm1_hs_phy_probe_report();
    assert(r->samples == 1 && f.slept == 1 && r->elapsed_ms == 10000);
    assert(r->status == FM1_HS_PHY_NO_RESET); /* Deadline prevents another sample. */

    init(&f); f.frozen_clock = 1; run(&f); r = fm1_hs_phy_probe_report();
    assert(r->samples == 500 && f.slept == 500 && r->elapsed_ms == 0);
    assert(r->status == FM1_HS_PHY_NO_RESET);

    init(&f); f.ready_after = 20000; run(&f); r = fm1_hs_phy_probe_report();
    assert(r->ready && r->polls == 20000 && r->samples == 500);
    puts("HS PHY probe: bounded readiness/deadline, reset/HS evidence, exact MMIO and teardown passed");
    return 0;
}
