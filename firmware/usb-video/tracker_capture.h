#ifndef FM1_TRACKER_CAPTURE_H
#define FM1_TRACKER_CAPTURE_H

#include "uvc_stream.h"
#include "view.h"

/* Render one or two RGB565 big-endian rows into caller-owned scratch sized
 * rows * FM1_UVC_WIDTH * 2. Width 256 centers the native 240-pixel tracker
 * between eight black pixels on each side; width 240 is unchanged. Invalid
 * pointers or rows/ranges return FM1_UVC_INVALID without writing anything.
 * This portable helper does no transport work and requires no IRQ guard. */
int fm1_tracker_capture_render(const fm1_tracker_view *view, unsigned first_row,
    unsigned rows, uint8_t *out);

/* SDK adapter contract for the tracker producer. Stream storage is static and
 * remains valid for the entire application lifetime. All portable stream-state
 * calls in the producer and adapter must be serialized on CPU0 with short local
 * IRQ guards. Rendering, conversion, and copying a logical view occur outside
 * those guards; packets stay reserved until publish/cancel releases ownership.
 * enabled()/get() are nonblocking queries made under the same local IRQ guard;
 * the core rejects publication if a reset races the unguarded render step. */
fm1_uvc_stream *fm1_uvc_stream_get(void);
int fm1_uvc_capture_enabled(void);

/* Stop capture and the adapter's endpoint activity before tracker quiescence.
 * The adapter preserves the caller's IRQ mask around bounded, nonwaiting
 * DMA/endpoint teardown. It must not render or wait while IRQs are masked. */
void fm1_uvc_capture_stop(void);

#endif
