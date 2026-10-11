#include "tracker_capture.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define NATIVE_ROW_BYTES 480u
#define WIRE_ROW_BYTES (FM1_UVC_WIDTH * 2u)
#define GUARD_BYTES 16u
#define SCRATCH_BYTES (FM1_UVC_ROWS_PER_PACKET * WIRE_ROW_BYTES)

static fm1_tracker_view view;
static uint8_t native_rgb[NATIVE_ROW_BYTES * FM1_UVC_HEIGHT];
static uint8_t wire_rgb[FM1_UVC_FRAME_BYTES];
static uint8_t reference_yuy2[FM1_UVC_FRAME_BYTES];
static uint8_t received_yuy2[FM1_UVC_FRAME_BYTES];
static uint8_t guarded[GUARD_BYTES + SCRATCH_BYTES + GUARD_BYTES];
static fm1_uvc_stream stream;

static void fixture(unsigned page, unsigned loaded)
{
    unsigned row, channel, x;
    memset(&view, 0, sizeof(view));
    view.page=(uint8_t)page;view.loaded=(uint8_t)loaded;
    view.channel=2;view.field=3;view.write=1;
    for(row=0;row<10;row++){
        snprintf(view.ui[row],sizeof(view.ui[row]),"ROW %02u TEST %c%u",row,'A'+row,row*13u);
        snprintf(view.grid[row],sizeof(view.grid[row]),"%02u C-3 %02X %1X%02X %c",row,row+1,row%16,row*9u,'A'+row);
    }
    for(channel=0;channel<4;channel++){
        view.peak[channel]=(uint8_t)(9u+channel*17u);
        for(x=0;x<48;x++)view.scope[channel][x]=(int8_t)((int)((x*13u+channel*23u)%127u)-63);
    }
    view.block_mask[1]=3;view.block_mask[4]=12;
}

static void reference_rgb(void)
{
    unsigned row;
    fm1_tracker_view_render(&view,0,FM1_UVC_HEIGHT,native_rgb);
    memset(wire_rgb,0,sizeof(wire_rgb));
    for(row=0;row<FM1_UVC_HEIGHT;row++)
        memcpy(wire_rgb+row*WIRE_ROW_BYTES+(WIRE_ROW_BYTES-NATIVE_ROW_BYTES)/2u,
            native_rgb+row*NATIVE_ROW_BYTES,NATIVE_ROW_BYTES);
}

static void assert_guarded_rows(unsigned first_row, unsigned rows)
{
    unsigned i;uint8_t *out=guarded+GUARD_BYTES;
    memset(guarded,0xa5,sizeof(guarded));
    assert(fm1_tracker_capture_render(&view,first_row,rows,out)==FM1_UVC_OK);
    assert(!memcmp(out,wire_rgb+first_row*WIRE_ROW_BYTES,rows*WIRE_ROW_BYTES));
    for(i=0;i<GUARD_BYTES;i++)assert(guarded[i]==0xa5);
    for(i=GUARD_BYTES+rows*WIRE_ROW_BYTES;i<sizeof(guarded);i++)assert(guarded[i]==0xa5);
#if FM1_UVC_WIDTH == 256u
    for(i=0;i<rows;i++){
        unsigned byte;
        for(byte=0;byte<16u;byte++){
            assert(out[i*WIRE_ROW_BYTES+byte]==0);
            assert(out[(i+1u)*WIRE_ROW_BYTES-16u+byte]==0);
        }
        assert(!memcmp(out+i*WIRE_ROW_BYTES+16u,
            native_rgb+(first_row+i)*NATIVE_ROW_BYTES,NATIVE_ROW_BYTES));
    }
#endif
}

static void consecutive_rows_match_native_view(void)
{
    unsigned profile,row;
    for(profile=0;profile<4;profile++){
        fixture(profile%3u,profile!=3u);reference_rgb();
        for(row=0;row<FM1_UVC_HEIGHT;row++){
            assert_guarded_rows(row,1);
            if(row+1u<FM1_UVC_HEIGHT)assert_guarded_rows(row,2);
        }
    }
}

static void invalid_requests_do_not_write(void)
{
    uint8_t baseline[sizeof(guarded)];uint8_t *out=guarded+GUARD_BYTES;
    memset(guarded,0xa5,sizeof(guarded));memcpy(baseline,guarded,sizeof(guarded));
    assert(fm1_tracker_capture_render(0,0,1,out)==FM1_UVC_INVALID);
    assert(fm1_tracker_capture_render(&view,0,1,0)==FM1_UVC_INVALID);
    assert(fm1_tracker_capture_render(&view,0,0,out)==FM1_UVC_INVALID);
    assert(fm1_tracker_capture_render(&view,0,3,out)==FM1_UVC_INVALID);
    assert(fm1_tracker_capture_render(&view,0,UINT_MAX,out)==FM1_UVC_INVALID);
    assert(fm1_tracker_capture_render(&view,239,2,out)==FM1_UVC_INVALID);
    assert(fm1_tracker_capture_render(&view,240,1,out)==FM1_UVC_INVALID);
    assert(fm1_tracker_capture_render(&view,UINT_MAX,1,out)==FM1_UVC_INVALID);
    assert(!memcmp(guarded,baseline,sizeof(guarded)));
}

static void reconstruct_frames_from_fragment_rows(void)
{
    unsigned pair,frame,packet_count=FM1_UVC_FRAME_BYTES/FM1_UVC_PAYLOAD_BYTES;
    fixture(0,1);reference_rgb();
    assert(FM1_UVC_PAYLOAD_BYTES==640u);
    assert(packet_count==(FM1_UVC_WIDTH==240u?180u:192u));
    assert(packet_count*FM1_UVC_PAYLOAD_BYTES==FM1_UVC_FRAME_BYTES);
    /* Pair-sized reference conversion is independent of packet boundaries. */
    for(pair=0;pair<FM1_UVC_FRAME_BYTES;pair+=4u)
        assert(fm1_uvc_rgb565be_to_yuy2(reference_yuy2+pair,wire_rgb+pair,2)==FM1_UVC_OK);
    fm1_uvc_init(&stream);fm1_uvc_start(&stream,1000);
    for(frame=0;frame<2;frame++){
        unsigned packet,bytes=0,eof_count=0,crossings=0;
        uint32_t start=1000u+frame*FM1_UVC_FRAME_PERIOD_MS;
        memset(received_yuy2,0xa5,sizeof(received_yuy2));
        assert(fm1_uvc_begin_frame(&stream,start)==FM1_UVC_OK);
        for(packet=0;packet<packet_count;packet++){
            fm1_uvc_write write;fm1_uvc_read read;unsigned offset,rows,i;
            uint8_t *rgb=guarded+GUARD_BYTES;
            uint8_t expected_header=(uint8_t)(FM1_UVC_HEADER_EOH | frame |
                (packet+1u==packet_count?FM1_UVC_HEADER_EOF:0u));
            assert(fm1_uvc_reserve_bytes(&stream,start+packet,&write)==FM1_UVC_OK);
            assert(write.first_byte==bytes && write.first_byte==packet*640u);
            assert(write.first_row==write.first_byte/WIRE_ROW_BYTES);
            assert(write.length==642u);
            offset=write.first_byte%WIRE_ROW_BYTES;
            rows=(offset+640u+WIRE_ROW_BYTES-1u)/WIRE_ROW_BYTES;
            assert(write.rows==rows && rows>=1 && rows<=2);
            assert(!(offset&3u) && offset+640u<=rows*WIRE_ROW_BYTES);
            if(offset)++crossings;
            memset(guarded,0xa5,sizeof(guarded));
            assert(fm1_tracker_capture_render(&view,write.first_row,write.rows,rgb)==FM1_UVC_OK);
            assert(!memcmp(rgb,wire_rgb+write.first_row*WIRE_ROW_BYTES,rows*WIRE_ROW_BYTES));
            assert(fm1_uvc_rgb565be_to_yuy2(write.bytes+2,rgb+offset,320)==FM1_UVC_OK);
            for(i=0;i<GUARD_BYTES;i++)assert(guarded[i]==0xa5);
            for(i=GUARD_BYTES+rows*WIRE_ROW_BYTES;i<sizeof(guarded);i++)assert(guarded[i]==0xa5);
            assert(fm1_uvc_publish_bytes(&stream,start+packet,&write)==FM1_UVC_OK);
            assert(fm1_uvc_acquire(&stream,&read)==FM1_UVC_OK);
            assert(read.length==642u && read.bytes[0]==2u && read.bytes[1]==expected_header);
            assert(!memcmp(read.bytes+2,reference_yuy2+bytes,640u));
            memcpy(received_yuy2+bytes,read.bytes+2,640u);bytes+=640u;
            eof_count+=(read.bytes[1]&FM1_UVC_HEADER_EOF)!=0;
            assert(fm1_uvc_commit(&stream,&read,1)==FM1_UVC_OK);
        }
        assert(crossings && eof_count==1 && bytes==FM1_UVC_FRAME_BYTES);
        assert(!memcmp(received_yuy2,reference_yuy2,sizeof(received_yuy2)));
        assert(!stream.frame_active && stream.stats.frames_sent==frame+1u);
        assert(stream.stats.packets_sent==(frame+1u)*packet_count);
    }
}

int main(void)
{
#if defined(FM1_UVC_IPAD_PAD) && FM1_UVC_IPAD_PAD
    assert(FM1_UVC_WIDTH==256u);
#else
    assert(FM1_UVC_WIDTH==240u);
#endif
    consecutive_rows_match_native_view();invalid_requests_do_not_write();
    reconstruct_frames_from_fragment_rows();
    printf("tracker capture %ux%u rows and fragmented frames passed\n",FM1_UVC_WIDTH,FM1_UVC_HEIGHT);
    return 0;
}
