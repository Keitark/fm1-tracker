#ifndef FM1_NES_H
#define FM1_NES_H
#include <stddef.h>
#include <stdint.h>

enum { FM1_NES_OK=0, FM1_NES_INVALID=-1, FM1_NES_UNSUPPORTED=-2,
       FM1_NES_IO_ERROR=-3, FM1_NES_BSP_UNVERIFIED=-4, FM1_NES_BUSY=-5 };
/* Controller 1, pressed bits. Knob/button-to-pad mapping is a BSP responsibility. */
enum { FM1_PAD_A=1, FM1_PAD_B=2, FM1_PAD_SELECT=4, FM1_PAD_START=8,
       FM1_PAD_UP=16, FM1_PAD_DOWN=32, FM1_PAD_LEFT=64, FM1_PAD_RIGHT=128 };
typedef struct {
    void *context;
    /* Consume/copy synchronously: buffers are reused immediately on return. */
    int (*video)(void *, unsigned y, unsigned rows, const uint16_t *rgb565);
    /* Core samples are unipolar unsigned amplitudes at 44100 Hz, NOT signed PCM.
       Board adapter must handle DC removal, format conversion, and safe gain. */
    int (*audio)(void *, const uint8_t *, size_t samples);
    uint8_t (*buttons)(void *);
    /* Called after each frame. Pace here; nonzero requests a clean stop. */
    int (*frame)(void *, uint32_t frame_number);
    /* Optional pre-mix voices, raw0..15: pulse1,pulse2,triangle,noise.
       Replaces audio when the channel-capture APU is built. Mixed bytes retain
       the original core rounding. Buffers valid only during this call. */
    int (*audio_channels)(void *,const uint8_t *mixed,const uint8_t *const voices[4],size_t);
    /* Optional once-per-frame rendering decision. Zero suppresses pixels only;
       CPU, APU, scroll, sprite overflow and sprite-zero hit still execute. */
    int (*render_frame)(void *);
} fm1_nes_platform;
typedef struct {
    uint32_t frames, video_blocks, audio_samples;
    size_t state_bytes;
    uint8_t ram0, ram1; /* Diagnostic ROM mailboxes only. */
} fm1_nes_stats;

int fm1_nes_validate_rom(const uint8_t *, size_t);
/* Single instance, not thread-safe. ROM storage must remain readable until return.
   frames==0 means run until frame callback requests stop. */
int fm1_nes_run(const uint8_t *, size_t, const fm1_nes_platform *,
                uint32_t frames, fm1_nes_stats *);
/* Requires explicitly installed, verified board services on pi32v2.
   Otherwise returns BSP_UNVERIFIED without accessing peripherals. */
int fm1_nes_target_start(const uint8_t *, size_t);
/* Build-overlay bridge; normally only called by the APU. */
int fm1_nes_sound_channels(const uint8_t *,const uint8_t *const[4],size_t);
int fm1_nes_render_frame(void);
#endif
