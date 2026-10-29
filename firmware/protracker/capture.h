#ifndef FM1_TRACKER_CAPTURE_H
#define FM1_TRACKER_CAPTURE_H
#include "view.h"
#include "uvc_stream.h"

/* Foreground CPU storage only: one immutable view and at most two padded
 * RGB565 rows. No additional framebuffer, DMA storage or heap allocation. */
typedef struct {
    fm1_tracker_view view;
    uint8_t rgb[FM1_UVC_ROWS_PER_PACKET*256u*2u];
    uint8_t valid;
} fm1_tracker_capture;

void fm1_tracker_capture_init(fm1_tracker_capture *);
void fm1_tracker_capture_accept(fm1_tracker_capture *, const fm1_tracker_view *);
void fm1_tracker_capture_cancel(fm1_tracker_capture *);
/* Caller owns an authenticated stream reservation and publishes/cancels it
 * afterwards. Render and convert with IRQs enabled, outside transport locks.
 * Only payload bytes [HEADER_BYTES,length) are written; headers stay intact. */
int fm1_tracker_capture_packet(fm1_tracker_capture *, const fm1_uvc_write *);
#endif
