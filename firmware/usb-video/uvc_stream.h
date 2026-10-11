#ifndef FM1_UVC_STREAM_H
#define FM1_UVC_STREAM_H

#include <stdint.h>

/* Portable UVC 1.0 YUY2 producer/transport core; no SDK, heap, or device I/O. */
#define FM1_UVC_WIDTH 240u
#define FM1_UVC_HEIGHT 240u
#define FM1_UVC_FRAME_BYTES (FM1_UVC_WIDTH * FM1_UVC_HEIGHT * 2u)
#define FM1_UVC_FRAME_PERIOD_MS 200u
#define FM1_UVC_ISO_MAX_PACKET 1023u
#define FM1_UVC_HEADER_BYTES 2u
#define FM1_UVC_ROWS_PER_PACKET 2u
#define FM1_UVC_PACKET_BYTES (FM1_UVC_HEADER_BYTES + FM1_UVC_WIDTH * 4u)
#define FM1_UVC_QUEUE_SLOTS 3u
#define FM1_UVC_HEADER_FID 0x01u
#define FM1_UVC_HEADER_EOF 0x02u
#define FM1_UVC_HEADER_ERR 0x40u
#define FM1_UVC_HEADER_EOH 0x80u

enum fm1_uvc_result {
    FM1_UVC_OK = 1,
    FM1_UVC_WAIT = 0,
    FM1_UVC_INVALID = -1,
    FM1_UVC_STALE = -2,
    FM1_UVC_DROPPED = -3,
    FM1_UVC_RETRY = -4
};

enum fm1_uvc_slot_state {
    FM1_UVC_FREE, FM1_UVC_WRITING, FM1_UVC_READY, FM1_UVC_LEASED
};

typedef struct {
    uint32_t epoch, token, frame, sequence;
    uint16_t length;
    uint8_t state, discard;
    uint8_t bytes[FM1_UVC_PACKET_BYTES];
} fm1_uvc_packet;

typedef struct {
    uint32_t frames_started, frames_sent, frames_dropped;
    uint32_t packets_sent, retries, backpressure, stale_releases;
} fm1_uvc_stats;

typedef struct {
    fm1_uvc_packet packets[FM1_UVC_QUEUE_SLOTS];
    fm1_uvc_stats stats;
    uint32_t epoch, token, frame, sequence;
    uint32_t next_frame_ms;
    uint32_t abort_frame;
    uint16_t next_row;
    uint8_t active, frame_active, fid, abort_fid, abort_pending;
} fm1_uvc_stream;

/* Producer token: only payload bytes [2,length) may be written outside the
 * critical section. Call publish/cancel even if a reset happened meanwhile. */
typedef struct {
    uint8_t *bytes;
    uint32_t epoch, token, frame;
    uint16_t length, first_row;
    uint8_t slot, rows;
} fm1_uvc_write;

/* Consumer token: storage remains stable until authentic commit, including
 * across stop/reset/start. One consumer lease may be outstanding at a time. */
typedef struct {
    const uint8_t *bytes;
    uint32_t epoch, token;
    uint16_t length;
    uint8_t slot;
} fm1_uvc_read;

/* Ownership and synchronization:
 * - Initialize once before IRQs/producer work begin. Never reinitialize a live
 *   stream: outstanding leases must first be committed/cancelled.
 * - All APIs except rgb565be_to_yuy2 must be serialized by caller-provided short
 *   critical sections against the USB IRQ and peripheral task. The core does
 *   not disable interrupts or spin. Each API visits at most three slots.
 * - reserve_rows -> convert/write payload OUTSIDE the critical section ->
 *   publish_rows. Only one producer reservation is outstanding at a time.
 * - acquire -> copy/submit prepared bytes -> commit(write_succeeded). A failed
 *   nonblocking write retains the exact packet for retry. For zero-copy DMA,
 *   commit must wait for DMA completion; for copied endpoint DMA it may occur
 *   once the writer has copied every byte into its separately owned buffer.
 * - No rendering/conversion/waits occur in acquire/commit or other core APIs.
 * - Times are uint32_t milliseconds; deadline comparison is wrap-safe when
 *   successive calls are separated by less than 2^31 milliseconds.
 */
void fm1_uvc_init(fm1_uvc_stream *stream);
void fm1_uvc_start(fm1_uvc_stream *stream, uint32_t now_ms);
void fm1_uvc_stop(fm1_uvc_stream *stream);
/* USB reset stops the stream. Host alternate-setting enable calls start. */
void fm1_uvc_reset(fm1_uvc_stream *stream);
int fm1_uvc_begin_frame(fm1_uvc_stream *stream, uint32_t now_ms);
int fm1_uvc_reserve_rows(fm1_uvc_stream *stream, uint32_t now_ms,
                         uint8_t rows, fm1_uvc_write *write);
int fm1_uvc_publish_rows(fm1_uvc_stream *stream, uint32_t now_ms,
                         const fm1_uvc_write *write);
int fm1_uvc_cancel_rows(fm1_uvc_stream *stream, uint32_t now_ms,
                        const fm1_uvc_write *write);
/* Abort only the current incomplete frame. Prepared packets belonging to that
 * frame are discarded; already transmitted bytes are followed by ERR+EOF so
 * the host can reject that frame. Completed older frames stay in FIFO order. */
int fm1_uvc_abort_frame(fm1_uvc_stream *stream, uint32_t now_ms);
int fm1_uvc_acquire(fm1_uvc_stream *stream, fm1_uvc_read *read);
int fm1_uvc_commit(fm1_uvc_stream *stream, const fm1_uvc_read *read,
                    int write_succeeded);

/* Convert an even number of RGB565 big-endian pixels. RGB channels expand to
 * 8-bit; BT.601 limited-range Y/U/V use signed integer arithmetic and chroma is
 * averaged per adjacent pair. Source and destination must not overlap. */
int fm1_uvc_rgb565be_to_yuy2(uint8_t *destination, const uint8_t *source,
                             uint32_t pixels);

#endif
