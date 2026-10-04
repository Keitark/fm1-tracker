#ifndef FM1_BOARD_H
#define FM1_BOARD_H
#include "fm1_nes.h"

#define FM1_LCD_WIDTH 240u
#define FM1_LCD_HEIGHT 240u
#define FM1_LCD_STRIP_ROWS 8u
#define FM1_KEY_COUNT 41u
#define FM1_KEY_UNASSIGNED 255u
#define FM1_NOTE_KEY_COUNT 12u
#define FM1_AUDIO_CAPACITY 4096u
#define FM1_AUDIO_PRIME 1470u /* Two emulated frames / 33.3 ms at 44100 Hz. */
#define FM1_AUDIO_TARGET 2205u /* Three frames / 50 ms; below ring capacity. */

/* All callbacks run on the emulator thread except the separately exposed audio
   queue consumer. Transfers must finish/copy before returning. Nonzero = error.
   now_us is a wrapping monotonic 32-bit clock. wait_us waits at least the supplied
   duration, services the watchdog and allows audio IRQs (or returns an error).
   read_keys supplies normalized pressed bits, not GPIO IDs.
   The producer and ISR must use the same IRQ-safe lock around queue operations. */
typedef struct {
    void *context;
    int (*lcd_write)(void *, int data, const uint8_t *, size_t);
    int (*delay_ms)(void *, uint32_t);
    uint32_t (*now_us)(void *);
    int (*wait_us)(void *, uint32_t);
    uint64_t (*read_keys)(void *);
    int (*pcm_write)(void *, const int16_t *mono, size_t frames);
    int (*stop_requested)(void *);
    /* Optional: audio-clock pacing. Snapshot queued MONO frames under the
       consumer lock; no waiting. With this callback, max_frame_skip must be
       nonzero. NULL retains the ordinary wall-clock scheduling path. */
    uint32_t (*audio_buffered)(void *);
    /* Optional task-side replacement for mono conversion, used by channel FX. */
    int (*audio_channels)(void *,const uint8_t *,const uint8_t *const[4],size_t);
    /* Optional paired asynchronous display callbacks. begin runs EVERY frame,
       including audio skips: service completion, then reserve if wanted.
       Returns 1=reserved, 0=skip, negative=fault. rows COPIES wire bytes before
       returning; it must never retain board.wire or the core's half-frame. */
    int (*video_begin)(void *,int wanted);
    int (*video_rows)(void *,unsigned y,unsigned rows,const uint8_t *wire);
    /* Alternative to video_rows: consume/copy native RGB565 pixels directly.
       Source stride is256 pixels; rows<=8. No pointer retained on return.
       crop=0 uses floor(x*256/240), crop=1 selects x+8. Requires video_begin;
       exactly one of video_rows/video_native_rows may be provided. */
    int (*video_native_rows)(void *,unsigned y,unsigned rows,const uint16_t *,unsigned crop);
} fm1_board_io;

typedef struct {
    uint8_t key_for_pad[8]; /* A, B, Select, Start, Up, Down, Left, Right. */
    uint8_t gain; /* 0 = mute (default); max128: |8-bit minus DC|*128 <=32640. */
    uint8_t crop; /* 0: nearest-neighbor fit 256 -> 240; 1: crop 8 each side. */
    uint8_t max_frame_skip; /* 0=off; 1..5 maximum consecutive LCD-only skips. */
    uint16_t x_offset, y_offset; /* Must be bench-confirmed; no implicit offset. */
} fm1_board_config;

typedef struct {
    fm1_board_io io;
    fm1_board_config config;
    fm1_nes_platform platform;
    uint8_t wire[FM1_LCD_WIDTH * FM1_LCD_STRIP_ROWS * 2];
    int16_t pcm[64];
    int32_t dc_q8;
    uint32_t deadline, remainder, candidate_since[8];
    uint8_t candidate, stable, filter_started, fault, running;
    uint8_t skip_video,skip_streak;
    uint32_t presented_frames,skipped_frames;
} fm1_board;

void fm1_board_default_config(fm1_board_config *);
/* Leftmost occurrence of each note, in pitch-class ARRAY order:
   C,C#,D,D#,E,F,F#,G,G#,A,A#,B. Physical keyboard starts at F, so this
   array order is NOT physical left-to-right order; see CONTROLS.md.
   Each entry is its measured scanner slot (0..40), NOT a MIDI note or GPIO.
   Unmeasured entries are KEY_UNASSIGNED. All eight used notes must be measured.
   C/D/E = Left/Down/Right; D# = Up; G/A = B/A; C#/F# = Select/Start.
   Only key_for_pad changes, atomically on success; no peripheral accesses. */
int fm1_board_assign_note_keys(fm1_board_config *,const uint8_t note_slots[FM1_NOTE_KEY_COUNT]);
/* Configuration only; no peripheral accesses. Unassigned buttons are allowed
   for a display/audio diagnostic, but are not a playable physical mapping. */
int fm1_board_construct(fm1_board *, const fm1_board_io *, const fm1_board_config *);
/* Call only AFTER board clocks, pins, reset, power and panel offset are verified.
   Runs recovered panel commands, clears the screen, then enables the display. */
int fm1_board_lcd_init(fm1_board *);
int fm1_board_run(fm1_board *, const uint8_t *, size_t, uint32_t, fm1_nes_stats *);

/* No internal synchronization: caller MUST serialize producer/consumer/reset.
   All-or-nothing enqueue. Overflow leaves queue unchanged. Underrun is silence.
   Output buffer is aligned native-endian signed 16-bit interleaved L/R PCM. */
typedef struct {
    int16_t mono[FM1_AUDIO_CAPACITY];
    uint32_t read_pos, write_pos, underrun_frames;
    uint32_t priming_frames,rebuffer_events;
    unsigned primed;
} fm1_audio_queue;
void fm1_audio_queue_reset(fm1_audio_queue *);
int fm1_audio_queue_push(fm1_audio_queue *, const int16_t *, size_t);
void fm1_audio_queue_stereo(fm1_audio_queue *, int16_t *, size_t);
/* Signed 24-bit samples in native-endian 32-bit words, interleaved L/R.
   Scales signed-16 mono by 256; right-aligned sample with sign extension. */
void fm1_audio_queue_stereo24(fm1_audio_queue *, int32_t *, size_t);

/* Stock-derived digital startup envelope, NOT hardware amplifier mute.
   Own on the audio consumer; reset before IRQ enable. Process exactly one
   64-frame stereo DMA half (128 signed-24 values in 32-bit words) per call.
   The stock 44118-frame countdown rounds up to 690 complete halves, followed
   by Q7 gain steps 1..127. No stock DSP tables or addresses are executed. */
typedef struct {
    uint32_t frames_left;
    uint8_t gain_q7;
    uint8_t target_q7; /*0..127, reset127; volume input may set before process. */
} fm1_audio_startup;
void fm1_audio_startup_reset(fm1_audio_startup *);
void fm1_audio_startup_process24(fm1_audio_startup *, int32_t stereo[128]);
/* Audio-priority consumer: prefill/rebuffer, mono->stereo24 and startup gain
   in one bounded pass. Same lock ownership as the queue; no waits/logging.
   Initial prefill silence is separate from post-start starvation counters. */
void fm1_audio_queue_play24(fm1_audio_queue *,fm1_audio_startup *,int32_t stereo[128]);
#endif
