#ifndef FM1_WL82_KEYSCAN_H
#define FM1_WL82_KEYSCAN_H
#include "fm1_stock_keys.h"

/* Physical SPI2 scanner for the stock-derived FM-1_010 pin assignment.
   start/poll/stop perform MMIO; construction/zero initialization does not.
   Caller must own SPI2, PA1/3/4/9/10, PH6/9 and the six column inputs exclusively,
   have established the board clocks/power, and supply a real monotonic clock.
   Zero-initialize the state before its first start. Call from ONE task, never
   an ISR for the legacy API. The opt-in async API below has explicit ISR and
   synchronization requirements. No stock-code calls are used. FM1_KEYSCAN_DMA2 selects
   two-byte DMA with polled completion and one shared persistent RAM source;
   there must be only ONE active scanner instance. Default is BUF byte polling.
   LED-multiplex outputs are kept low; encoder rotations are not decoded.
   Not enabled automatically by the headless boot image. Hardware unqualified. */
/* Failure-only snapshot, taken before stop resets SPI2/row/latch. reason is
   a bitmask: 1=elapsed-time limit, 2=poll limit, 4=task-observed IRQ no-progress.
   phase: 0=high, 1=low byte,
   2=whole DMA word. dma_count is readable CNT at failure, not proof of progress.
   Retained through stop/failed poll; cleared only by an accepted start.
   Same-task access only; callers must copy before publishing to other tasks. */
typedef struct {
    uint32_t valid,reason,row,phase,value,start_us,end_us,polls;
    /* BAUD is write-only; baud_written is software intent, NOT readback. */
    uint32_t con,baud_written,mux,pa_out,pa_dir;
    uint32_t pre_con,cleared_con,first_con;
    uint32_t dma_count;
} fm1_wl82_keyscan_failure;
typedef struct {
    uint32_t init_con,init_mux,scan_con,scan_mux,scans;
    /* First mismatch against initialization, ignoring CON pending/clear bits
       and unrelated mux bits. Zero change_scan means none observed. */
    uint32_t change_scan,change_con,change_mux;
} fm1_wl82_keyscan_trace;
typedef struct {
    void *context;
    uint32_t (*now_us)(void *);
    uint8_t row, running;
    fm1_wl82_keyscan_failure failure;
    fm1_wl82_keyscan_trace trace;
#ifdef FM1_KEYSCAN_IRQ
    uint8_t async_mode,primed,work_rows[11],ready_rows[11];
    uint8_t paused;
    uint32_t completions,observed_completions,observed_at,sequence,consumed;
#endif
} fm1_wl82_keyscan;

int fm1_wl82_keyscan_start(fm1_wl82_keyscan *,void *,uint32_t (*now_us)(void *));
/* Publish only one complete 11-row scan. On error, leave *pressed unchanged,
   disable SPI2, and require a fresh start. Portable board layer debounces. */
int fm1_wl82_keyscan_poll(fm1_wl82_keyscan *,uint64_t *pressed);
/* Same physical sweep, preserving the encoder contacts for diagnostics. */
int fm1_wl82_keyscan_raw(fm1_wl82_keyscan *,uint8_t rows[FM1_STOCK_SCAN_ROWS]);
void fm1_wl82_keyscan_stop(fm1_wl82_keyscan *);
#ifdef FM1_KEYSCAN_IRQ
/* The caller must serialize ALL accesses with a cross-core spinlock and local
   IRQ masking. Install IRQ_SPI2 before start; mask/unregister before teardown.
   step handles ONE completion, never waits/decodes/logs/reads a clock.
   raw copies a newly completed sweep, or BUSY without altering rows. It checks
   lack of IRQ progress for >=10ms since its last task observation (30ms in
   the MOD async-LCD profile), not exact
   transfer duration. Call regularly; stop/reset/recovery are task-only. */
int fm1_wl82_keyscan_async_start(fm1_wl82_keyscan *,void *,uint32_t (*)(void *));
void fm1_wl82_keyscan_async_step(fm1_wl82_keyscan *);
/* PACED mode stops after each sweep, leaving the latch high and no DMA active.
   A periodic caller (under the same lock) kicks exactly one new sweep. It must
   run independently of NES frames, normally every 1ms. No waits/clock reads;
   duplicate kicks during a transfer and kicks after stop do nothing. The
   task watchdog still detects a lost pacing timer after 10ms without progress. */
void fm1_wl82_keyscan_async_kick(fm1_wl82_keyscan *);
int fm1_wl82_keyscan_async_raw(fm1_wl82_keyscan *,uint8_t rows[11]);
#endif
#endif
