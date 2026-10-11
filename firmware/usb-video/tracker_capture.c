#include "tracker_capture.h"
#include <string.h>

#if FM1_UVC_WIDTH != 240u && FM1_UVC_WIDTH != 256u
#error Unsupported tracker capture width
#endif

int fm1_tracker_capture_render(const fm1_tracker_view *view, unsigned first_row,
    unsigned rows, uint8_t *out)
{
    if (!view || !out || !rows || rows > FM1_UVC_ROWS_PER_PACKET ||
        first_row >= FM1_UVC_HEIGHT || rows > FM1_UVC_HEIGHT - first_row)
        return FM1_UVC_INVALID;
    fm1_tracker_view_render(view, first_row, rows, out);
#if FM1_UVC_WIDTH == 256u
    /* Expand from the final row backwards, so neither the row expansion nor
     * its black margins can overwrite a native row still needed as input. */
    while (rows) {
        unsigned row = --rows;
        uint8_t *destination = out + row * 512u;
        memmove(destination + 16u, out + row * 480u, 480u);
        memset(destination, 0, 16u);
        memset(destination + 496u, 0, 16u);
    }
#endif
    return FM1_UVC_OK;
}
