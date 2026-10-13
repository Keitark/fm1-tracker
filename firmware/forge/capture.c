#include "capture.h"
#include <string.h>
#if FM1_UVC_WIDTH != 240u && FM1_UVC_WIDTH != 256u
#error Unsupported Forge video width
#endif
void fm1_forge_capture_init(fm1_forge_capture *c){if(c)memset(c,0,sizeof(*c));}
void fm1_forge_capture_accept(fm1_forge_capture *c,const fm1_forge_frame *frame){
    if(c && frame){c->frame=*frame;c->valid=1;}
}
void fm1_forge_capture_cancel(fm1_forge_capture *c){if(c)c->valid=0;}
int fm1_forge_capture_packet(fm1_forge_capture *c,const fm1_uvc_write *w,
    fm1_forge_capture_renderer render){
    unsigned rows,offset,payload;
    if(!c || !c->valid || !w || !w->bytes || !w->rows ||
       w->rows>FM1_UVC_ROWS_PER_PACKET || w->first_row>=FM1_UVC_HEIGHT ||
       w->rows>FM1_UVC_HEIGHT-w->first_row || w->length<=FM1_UVC_HEADER_BYTES ||
       w->length>FM1_UVC_PACKET_BYTES || w->first_byte>=FM1_UVC_FRAME_BYTES ||
       w->first_byte/(FM1_UVC_WIDTH*2u)!=w->first_row)return FM1_UVC_INVALID;
    offset=w->first_byte%(FM1_UVC_WIDTH*2u);
    payload=w->length-FM1_UVC_HEADER_BYTES;
    if((w->first_byte&3u) || (payload&3u) ||
       offset+payload>w->rows*FM1_UVC_WIDTH*2u ||
       payload>FM1_UVC_FRAME_BYTES-w->first_byte)return FM1_UVC_INVALID;
    if(render)render(&c->frame,w->first_row,w->rows,c->rgb,16);
    else if(fm1_forge_render(&c->frame,w->first_row,w->rows,c->rgb,16))return FM1_UVC_INVALID;
#if FM1_UVC_WIDTH == 256u
    /* Same reverse expansion used by ProTracker: eight black pixels at each
     * side, with no overwrite of a native row still needed as input. */
    rows=w->rows;
    while(rows){unsigned row=--rows;uint8_t *out=c->rgb+row*512u;
        memmove(out+16u,c->rgb+row*480u,480u);memset(out,0,16u);memset(out+496u,0,16u);}
#else
    (void)rows;
#endif
    return fm1_uvc_rgb565be_to_yuy2(w->bytes+FM1_UVC_HEADER_BYTES,
        c->rgb+offset,payload/2u);
}
