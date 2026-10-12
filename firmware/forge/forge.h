/* FM1 Forge module ABI 1. The fixed runtime owns USB, clocks and device IO. */
#ifndef FM1_FORGE_H
#define FM1_FORGE_H
#include <stdint.h>
#include <stddef.h>
#define FM1_FORGE_ABI 1u
#define FM1_FORGE_KEYS 41u
#define FM1_FORGE_ENCODERS 7u
#define FM1_FORGE_RATE 44100u
#define FM1_FORGE_TONE_LIMIT 88200u
#define FM1_FORGE_TONE_PEAK 131072
#define FM1_FORGE_NO_NOTE 255u
/* Select user access; fixed diagnostic/recovery drivers always remain active. */
#ifndef FM1_FORGE_HAL_BUTTONS
#define FM1_FORGE_HAL_BUTTONS 1
#endif
#ifndef FM1_FORGE_HAL_ENCODERS
#define FM1_FORGE_HAL_ENCODERS 1
#endif
#ifndef FM1_FORGE_HAL_VOLUME
#define FM1_FORGE_HAL_VOLUME 1
#endif
#ifndef FM1_FORGE_HAL_LCD
#define FM1_FORGE_HAL_LCD 1
#endif
#ifndef FM1_FORGE_HAL_AUDIO
#define FM1_FORGE_HAL_AUDIO 1
#endif
#if (FM1_FORGE_HAL_BUTTONS != 0 && FM1_FORGE_HAL_BUTTONS != 1) || (FM1_FORGE_HAL_ENCODERS != 0 && FM1_FORGE_HAL_ENCODERS != 1) || (FM1_FORGE_HAL_VOLUME != 0 && FM1_FORGE_HAL_VOLUME != 1) || (FM1_FORGE_HAL_LCD != 0 && FM1_FORGE_HAL_LCD != 1) || (FM1_FORGE_HAL_AUDIO != 0 && FM1_FORGE_HAL_AUDIO != 1)
#error Forge HAL access selections must be zero or one
#endif
typedef struct {
    uint64_t keys,seen;
    int32_t encoder[7];
    uint32_t encoder_invalid[7];
    uint32_t scans,input_errors,volume_samples,volume_errors;
    uint16_t volume_raw,volume_min,volume_max;
    uint8_t encoder_seen,volume_valid,volume_gain,last_key;
} fm1_forge_input;
typedef struct {
    fm1_forge_input input;
    uint64_t candidate;
    uint32_t changed_at[41],last_good_ms;
    uint8_t initialized;
} fm1_forge_controls;
typedef struct {
    uint32_t phase,increment,frames;
    uint16_t envelope;
    uint8_t note,releasing;
} fm1_forge_voice;
typedef struct {
    fm1_forge_input input;
    uint32_t uptime_ms,ui_frames,lcd_completed,lcd_busy,lcd_errors;
    uint32_t audio_frames,audio_irqs,sys_hz,lsb_hz;
    uint16_t fps_tenths;
    int16_t fault;
    uint8_t usb_audio,note,module_user;
} fm1_forge_frame;
/* Caller supplies coherent scanner snapshots. All calls below are pure/no IO. */
void fm1_forge_controls_init(fm1_forge_controls *);
void fm1_forge_controls_scan(fm1_forge_controls *,uint64_t,uint32_t);
void fm1_forge_controls_stale(fm1_forge_controls *,uint32_t);
void fm1_forge_controls_encoders(fm1_forge_controls *,const int32_t[7],const uint32_t[7]);
void fm1_forge_controls_volume(fm1_forge_controls *,unsigned,unsigned,unsigned);
void fm1_forge_voice_init(fm1_forge_voice *);
void fm1_forge_voice_note(fm1_forge_voice *,unsigned);
void fm1_forge_voice_release(fm1_forge_voice *);
void fm1_forge_voice_render(fm1_forge_voice *,int32_t *,unsigned);
/* Clamp user output to the signed24 DMA contract before USB mix/master gain. */
void fm1_forge_audio_limit(int32_t *,unsigned);
unsigned fm1_forge_note_for_keys(uint64_t);
const char *fm1_forge_key_name(unsigned);
/* RGB565 pixels are big-endian; RGB444 packs each adjacent pair into 3 bytes.
 * Renders a fixed snapshot. Never retains output pointers or acquires locks. */
int fm1_forge_render(const fm1_forge_frame *,unsigned,unsigned,uint8_t *,unsigned);
/* Modules execute update/render on one task. audio is IRQ context, bounded to
 * 64 stereo frames; it must not wait, allocate, log, use IO, or change clocks.
 * Input is an immutable copy. User code cannot own USB, GPIO or flash. */
typedef struct {
    unsigned abi;
    const char *name;
    void (*init)(void);
    void (*update)(const fm1_forge_input *,uint32_t);
    void (*render)(const fm1_forge_frame *,unsigned,unsigned,uint8_t *,unsigned);
    void (*audio)(int32_t *,unsigned);
    void (*shutdown)(void);
} fm1_forge_module;
extern const fm1_forge_module fm1_forge_user_module;
#endif
