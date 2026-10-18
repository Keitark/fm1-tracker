#ifndef FM1_FORGE_CAPTURE_H
#define FM1_FORGE_CAPTURE_H
#include "forge.h"
#include "uvc_stream.h"

typedef void (*fm1_forge_capture_renderer)(const fm1_forge_frame *,unsigned,
    unsigned,uint8_t *,unsigned);
/* One logical frame plus at most two RGB565 rows; never a full framebuffer.
 * Only the foreground producer owns this object. User callbacks must respect
 * the immutable frame argument; unrelated user globals are not snapshotted. */
typedef struct {
    fm1_forge_frame frame;
    uint8_t rgb[FM1_UVC_ROWS_PER_PACKET*FM1_UVC_WIDTH*2u];
    uint8_t valid;
} fm1_forge_capture;
void fm1_forge_capture_init(fm1_forge_capture *);
void fm1_forge_capture_accept(fm1_forge_capture *,const fm1_forge_frame *);
void fm1_forge_capture_cancel(fm1_forge_capture *);
/* Render/convert with IRQs enabled and no locks held. The caller owns an
 * authenticated stream reservation and publishes/cancels it afterwards. */
int fm1_forge_capture_packet(fm1_forge_capture *,const fm1_uvc_write *,
    fm1_forge_capture_renderer);
#endif
