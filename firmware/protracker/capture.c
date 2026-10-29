#include "capture.h"
#include <string.h>

#if FM1_UVC_WIDTH != 240u && FM1_UVC_WIDTH != 256u
#error Tracker capture requires native 240 or padded 256 pixel video
#endif
#if FM1_UVC_HEIGHT != 240u || FM1_UVC_ROWS_PER_PACKET != 2u
#error Tracker capture requires 240 rows and a two-row scratch bound
#endif

void fm1_tracker_capture_init(fm1_tracker_capture *c){if(c)memset(c,0,sizeof(*c));}
void fm1_tracker_capture_accept(fm1_tracker_capture *c,const fm1_tracker_view *view){
    if(c && view){c->view=*view;c->valid=1;}
}
void fm1_tracker_capture_cancel(fm1_tracker_capture *c){if(c)c->valid=0;}
int fm1_tracker_capture_packet(fm1_tracker_capture *c,const fm1_uvc_write *w){
    const unsigned stride=FM1_UVC_WIDTH*2u;
    unsigned offset,payload,rows;
    if(!c || !c->valid || !w || !w->bytes || !w->rows ||
       w->rows>FM1_UVC_ROWS_PER_PACKET || w->first_row>=FM1_UVC_HEIGHT ||
       w->rows>FM1_UVC_HEIGHT-w->first_row || w->length<=FM1_UVC_HEADER_BYTES ||
       w->length>FM1_UVC_PACKET_BYTES || w->first_byte>=FM1_UVC_FRAME_BYTES ||
       w->first_row!=w->first_byte/stride)return FM1_UVC_INVALID;
    payload=w->length-FM1_UVC_HEADER_BYTES;offset=w->first_byte%stride;
    rows=(offset+payload+stride-1u)/stride;
    if((w->first_byte&3u) || (payload&3u) || rows!=w->rows ||
       payload>FM1_UVC_FRAME_BYTES-w->first_byte ||
       w->bytes[0]!=FM1_UVC_HEADER_BYTES || !(w->bytes[1]&FM1_UVC_HEADER_EOH) ||
       (w->bytes[1]&FM1_UVC_HEADER_ERR) ||
       !!(w->bytes[1]&FM1_UVC_HEADER_EOF)!=(w->first_byte+payload==FM1_UVC_FRAME_BYTES))
        return FM1_UVC_INVALID;
    fm1_tracker_view_render(&c->view,w->first_row,w->rows,c->rgb);
#if FM1_UVC_WIDTH == 256u
    /* Expand backwards so row 0 cannot overwrite row 1's native source. Each
     * row receives exactly eight black pixels before and after the LCD image. */
    while(rows){
        uint8_t *out=c->rgb+(--rows)*512u;
        memmove(out+16u,c->rgb+rows*480u,480u);
        memset(out,0,16u);memset(out+496u,0,16u);
    }
#endif
    return fm1_uvc_rgb565be_to_yuy2(w->bytes+FM1_UVC_HEADER_BYTES,c->rgb+offset,payload/2u);
}
