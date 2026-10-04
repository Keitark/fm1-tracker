#ifndef FM1_WL82_H
#define FM1_WL82_H
#include "fm1_board.h"

/* Experimental SDK-backed hardware adapter, not a startup/BSP replacement.
   Installation copies the configuration and does NO I/O. There is deliberately
   no default installation. Services must outlive target_start().
   prepare() establishes the profile's clocks, pin ownership, watchdog and
   IRQ-safe environment. The profile must document power/reset assumptions;
   success does NOT certify electrical safety or physical qualification.
   HARDWARE policy requires prepare() to leave the amplifier muted and mute()
   to control a verified mute signal. DIGITAL_ONLY explicitly supplies no
   physical mute and no callback; startup/shutdown transients remain unqualified.
   finish() releases resources after peripheral shutdown,
   including partially initialized resources on a failed prepare().
   Neither this driver nor the NES core touches flash or calls stock FW addresses. */
enum { FM1_AUDIO_MUTE_HARDWARE=0, FM1_AUDIO_MUTE_DIGITAL_ONLY=1 };
typedef struct {
    void *context;
    int (*prepare)(void *);
    int (*mute)(void *,int);
    void (*finish)(void *);
    uint32_t (*now_us)(void *);
    int (*wait_us)(void *,uint32_t);
    uint64_t (*read_keys)(void *);
    int (*stop_requested)(void *);
    /* Bind ALINK0 IRQ (SDK IRQ 11) to a proper ABI ISR wrapper that invokes the
       supplied ordinary C handler. Stop must synchronously disable/quiesce it.
       Keep producer and ISR on one CPU or supply cross-core-safe serialization. */
    int (*audio_irq_start)(void *,void (*handler)(void));
    void (*audio_irq_stop)(void *);
    unsigned (*irq_save)(void *);
    void (*irq_restore)(void *,unsigned);
    uint8_t mute_policy;
} fm1_wl82_services;

typedef struct {
    /* Explicit ALINK0 configuration; stock-derived is not bench-confirmed.
       PC0..PC6 overlap none of the LCD pins, but the DAC/amp wiring must be
       established before enabling any output. 24-bit stereo in 32-bit words. */
    uint8_t output_channel, mclk_output, update_edge;
    uint8_t sclk_32_per_frame; /* Must be zero: 24-bit stereo needs 64 clocks. */
} fm1_wl82_audio_config;
/* Populate this unit's FM-1_010 channel/clock route, without I/O.
   PC6 is channel 3 DATA, not an amplifier mute pin. Board services remain needed. */
void fm1_wl82_stock_audio_route(fm1_wl82_audio_config *);
int fm1_wl82_install(const fm1_wl82_services *,const fm1_board_config *,
                      const fm1_wl82_audio_config *);
int fm1_wl82_run(const uint8_t *,size_t);
int fm1_wl82_run_with_stats(const uint8_t *,size_t,fm1_nes_stats *);
#endif
