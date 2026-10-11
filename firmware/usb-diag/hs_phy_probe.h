#ifndef FM1_HS_PHY_PROBE_H
#define FM1_HS_PHY_PROBE_H

#include <stdint.h>

enum fm1_hs_phy_status {
    FM1_HS_PHY_NOT_RUN = 0,
    FM1_HS_PHY_READY_TIMEOUT = 1,
    FM1_HS_PHY_NO_RESET = 2,
    /* A fresh bus reset was observed, but not POWER.HSMODE. This is not
     * evidence of successful full-speed enumeration or transfers. */
    FM1_HS_PHY_RESET_NO_HS = 3,
    FM1_HS_PHY_HIGH_SPEED = 4
};

typedef struct fm1_hs_phy_report {
    uint32_t ready;
    uint32_t polls;          /* SIE readiness reads, at most 20000. */
    uint32_t reset_seen;     /* Fresh reset after soft attachment. */
    uint32_t high_seen;      /* POWER.HSMODE observed after that reset. */
    uint32_t power;          /* Last attached POWER snapshot, before hold. */
    uint32_t status;
    uint32_t samples;        /* Negotiation snapshots, at most 500. */
    uint32_t elapsed_ms;
} fm1_hs_phy_report;

/* Boot-only diagnostic: call once before starting USB0. It always disconnects
 * and holds USB1 before returning. HIGH_SPEED means PHY reset/chirp evidence,
 * not successful descriptor enumeration or working high-speed transfers. */
void fm1_hs_phy_probe_run(void);
const fm1_hs_phy_report *fm1_hs_phy_probe_report(void);

#ifdef FM1_HS_PHY_TEST
/* The same state machine runs against injected MMIO/time operations in tests.
 * Production has no writable operation table or caller-selected addresses. */
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
void fm1_hs_phy_probe_run_with_ops(const fm1_hs_phy_ops *ops);
#endif

#endif
