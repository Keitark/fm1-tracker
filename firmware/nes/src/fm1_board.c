#include "fm1_board.h"
#include <string.h>

/* Recovered from FM-1_010, app offset 0x5000c, 21 x 18-byte records.
   Record 1 is a 120 ms delay, NOT an LCD command. See BOARD_INTEGRATION.md. */
static const uint8_t panel_init[21][18] = {
    {0x11,0}, {0x45,120},
    {0x2a,4,0,0,0,0xef}, {0x2b,4,0,0x28,1,0x17},
    {0xb2,5,0x0c,0x0c,0x0c,0,0x33,0x33}, {0x20,0},
    {0xb7,1,0x56}, {0xbb,1,0x18}, {0xc0,1,0x2c},
    {0xc2,1,1}, {0xc3,1,0x1f}, {0xc4,1,0x20}, {0xc6,1,0x0f},
    {0xd0,2,0xa6,0xa1},
    {0xe0,14,0xd0,0x0d,0x14,0x0b,0x0b,7,0x3a,0x44,0x50,8,0x13,0x13,0x2d,0x32},
    {0xe1,14,0xd0,0x0d,0x14,0x0b,0x0b,7,0x3a,0x44,0x50,8,0x13,0x13,0x2d,0x32},
    {0x36,1,0}, {0x3a,1,0x55}, {0xe7,1,0}, {0x51,1,0xff}, {0x21,0}
};

static int command(fm1_board *b, uint8_t cmd, const uint8_t *data, size_t n) {
    if (b->io.lcd_write(b->io.context,0,&cmd,1)) return FM1_NES_IO_ERROR;
    if (n && b->io.lcd_write(b->io.context,1,data,n)) return FM1_NES_IO_ERROR;
    return 0;
}
static int window(fm1_board *b, unsigned y, unsigned rows) {
    unsigned x0=b->config.x_offset, x1=x0+239;
    unsigned y0=b->config.y_offset+y, y1=y0+rows-1;
    uint8_t x[]={ (uint8_t)(x0>>8),(uint8_t)x0,(uint8_t)(x1>>8),(uint8_t)x1 };
    uint8_t p[]={ (uint8_t)(y0>>8),(uint8_t)y0,(uint8_t)(y1>>8),(uint8_t)y1 };
    if (command(b,0x2a,x,4) || command(b,0x2b,p,4) || command(b,0x2c,0,0))
        return FM1_NES_IO_ERROR;
    return 0;
}
static int video(void *ctx,unsigned y,unsigned rows,const uint16_t *pixels) {
    fm1_board *b=ctx;
    unsigned start,r,x,count,group;
    if (!pixels || !rows || y>=240 || rows>240-y) return FM1_NES_INVALID;
    /* Drop the entire presentation, including conversion and both half-frame
       blocks. CPU/PPU/APU execution and input sampling are never skipped. */
    if(b->skip_video)return 0;
    if(b->io.video_native_rows) {
        for(start=0;start<rows;start+=count) {
            count=rows-start;if(count>FM1_LCD_STRIP_ROWS)count=FM1_LCD_STRIP_ROWS;
            if(b->io.video_native_rows(b->io.context,y+start,count,pixels+start*256u,b->config.crop))
                return FM1_NES_IO_ERROR;
        }
        return 0;
    }
    /* GRAM cursor advances across data chunks (as in the stock continuous
       fill). Set one window per callback, not once per eight-row DMA strip.
       Every callback establishes its own window, including nonsequential y. */
    if(!b->io.video_rows && window(b,y,rows))return FM1_NES_IO_ERROR;
    for (start=0;start<rows;start+=count) {
        count=rows-start; if(count>FM1_LCD_STRIP_ROWS)count=FM1_LCD_STRIP_ROWS;
        for(r=0;r<count;++r) {
            const uint16_t *src=pixels+(start+r)*256u;
            uint8_t *dst=b->wire+r*480u;
            if(b->config.crop) {
                src+=8;
                for(x=0;x<240;++x){uint16_t p=*src++;*dst++=(uint8_t)(p>>8);*dst++=(uint8_t)p;}
            } else {
                /* floor(x*256/240) == 16*(x/15)+(x%15). Copy15, skip1;
                   bit-identical nearest-neighbor scaling, no per-pixel divide. */
                for(group=0;group<16;++group) {
                    for(x=0;x<15;++x){uint16_t p=*src++;*dst++=(uint8_t)(p>>8);*dst++=(uint8_t)p;}
                    ++src;
                }
            }
        }
        if(b->io.video_rows ? b->io.video_rows(b->io.context,y+start,count,b->wire) :
           b->io.lcd_write(b->io.context,1,b->wire,count*240u*2u))
            return FM1_NES_IO_ERROR;
    }
    return 0;
}
static int audio(void *ctx,const uint8_t *samples,size_t n) {
    fm1_board *b=ctx;
    size_t start,i,count;
    if(!samples && n)return FM1_NES_INVALID;
    for(start=0;start<n;start+=count) {
        count=n-start;if(count>64)count=64;
        for(i=0;i<count;++i) {
            int32_t x=(int32_t)samples[start+i]*256;
            if(!b->filter_started){b->dc_q8=x;b->filter_started=1;}
            /* Q8 DC estimator; int division avoids implementation-defined
               negative right shifts. Residual quantization <= 1 sample unit. */
            b->dc_q8+=(x-b->dc_q8)/256;
            b->pcm[i]=(int16_t)(((x-b->dc_q8)*b->config.gain)/256);
        }
        if(b->io.pcm_write(b->io.context,b->pcm,count))return FM1_NES_IO_ERROR;
    }
    return 0;
}
static int audio_channels(void *ctx,const uint8_t *mixed,const uint8_t *const voices[4],size_t n) {
    fm1_board *b=ctx;return b->io.audio_channels(b->io.context,mixed,voices,n);
}
static uint8_t buttons(void *ctx) {
    fm1_board *b=ctx;
    uint64_t keys=b->io.read_keys(b->io.context);
    uint32_t now=b->io.now_us(b->io.context);
    unsigned i;
    uint8_t result;
    for(i=0;i<8;++i) {
        uint8_t mask=(uint8_t)(1u<<i),key=b->config.key_for_pad[i];
        int pressed=key<FM1_KEY_COUNT && ((keys>>key)&1u);
        if(pressed != !!(b->candidate&mask)) {
            b->candidate^=mask;b->candidate_since[i]=now;
        } else if((uint32_t)(now-b->candidate_since[i])>=10000u) {
            b->stable=(uint8_t)((b->stable & ~mask)|(b->candidate&mask));
        }
    }
    result=b->stable;
    /* Never emit impossible opposite directions. */
    if((result&(FM1_PAD_UP|FM1_PAD_DOWN))==(FM1_PAD_UP|FM1_PAD_DOWN))
        result&=(uint8_t)~(FM1_PAD_UP|FM1_PAD_DOWN);
    if((result&(FM1_PAD_LEFT|FM1_PAD_RIGHT))==(FM1_PAD_LEFT|FM1_PAD_RIGHT))
        result&=(uint8_t)~(FM1_PAD_LEFT|FM1_PAD_RIGHT);
    return result;
}
static int render_frame(void *ctx) {
    fm1_board *b=ctx;
    if(b->io.video_begin) {
        int rc=b->io.video_begin(b->io.context,!b->skip_video);
        if(rc<0)b->fault=1;
        b->skip_video=rc<=0;
    }
    return !b->skip_video;
}
static int frame(void *ctx,uint32_t number) {
    fm1_board *b=ctx;
    uint32_t now,left;
    (void)number;
    if(b->skip_video){++b->skipped_frames;if(b->skip_streak<255)++b->skip_streak;}
    else {++b->presented_frames;b->skip_streak=0;}
    if(b->io.stop_requested(b->io.context) || b->fault)return 1;
    if(b->io.audio_buffered) {
        uint32_t queued=b->io.audio_buffered(b->io.context);
        /* The DAC, not an unrelated video deadline, is the master clock.
           Refill without sleeping when low. Wait only whole 10ms RTOS ticks
           that leave a 50ms reserve; never round a tiny wait up to a tick. */
        if(queued>=FM1_AUDIO_TARGET+441u) {
            uint32_t ticks=(queued-FM1_AUDIO_TARGET)/441u;
            if(ticks>2)ticks=2;
            if(b->io.wait_us(b->io.context,ticks*10000u)){b->fault=1;return 1;}
            queued=b->io.audio_buffered(b->io.context);
        }
        /* Both halves share this decision. Emulated CPU/PPU/APU and input
           still run every frame. Force a display after the bounded streak. */
        b->skip_video=queued<FM1_AUDIO_TARGET && b->skip_streak<b->config.max_frame_skip;
        return 0;
    }
    b->remainder+=1000000u;
    b->deadline+=b->remainder/60u;b->remainder%=60u;
    now=b->io.now_us(b->io.context);
    /* Wrap-safe for intervals < 2^31 us; don't busy-catch-up missed frames. */
    left=b->deadline-now;
    if(left && left<0x80000000u) {
        if(b->io.wait_us(b->io.context,left)){b->fault=1;return 1;}
    } else if(left) {
        if(!b->config.max_frame_skip)b->deadline=now;
        /* Bound catch-up after a long stall; never build an unlimited debt. */
        else if(now-b->deadline>100000u)b->deadline=now-100000u;
    }
    now=b->io.now_us(b->io.context);
    left=now-b->deadline;
    /* Ignore sub-frame timing jitter. Retain the absolute deadline while
       catching up, rather than resetting it on every late frame. Force one
       visible frame after the configured consecutive-skip limit. */
    b->skip_video=b->config.max_frame_skip && left>=16667u && left<0x80000000u &&
                  b->skip_streak<b->config.max_frame_skip;
    return 0;
}
void fm1_board_default_config(fm1_board_config *c) {
    memset(c,0,sizeof(*c));memset(c->key_for_pad,FM1_KEY_UNASSIGNED,8);
}
int fm1_board_assign_note_keys(fm1_board_config *c,const uint8_t note_slots[FM1_NOTE_KEY_COUNT]) {
    /* Pad bit order: A, B, Select, Start, Up, Down, Left, Right. */
    static const uint8_t note_for_pad[8]={9,7,1,6,3,2,0,4};
    uint8_t assigned[8];
    uint64_t seen=0;
    unsigned i;
    if(!c || !note_slots)return FM1_NES_INVALID;
    for(i=0;i<FM1_NOTE_KEY_COUNT;++i) {
        uint8_t slot=note_slots[i];
        uint64_t bit;
        if(slot==FM1_KEY_UNASSIGNED)continue;
        if(slot>=FM1_KEY_COUNT)return FM1_NES_INVALID;
        bit=(uint64_t)1<<slot;
        if(seen&bit)return FM1_NES_INVALID;
        seen|=bit;
    }
    for(i=0;i<8;++i) {
        assigned[i]=note_slots[note_for_pad[i]];
        if(assigned[i]==FM1_KEY_UNASSIGNED)return FM1_NES_BSP_UNVERIFIED;
    }
    memcpy(c->key_for_pad,assigned,sizeof(assigned));
    return 0;
}
int fm1_board_construct(fm1_board *b,const fm1_board_io *io,const fm1_board_config *c) {
    unsigned i;
    if(!b || !io || !c || !io->lcd_write || !io->delay_ms || !io->now_us ||
       !io->wait_us || !io->read_keys || !io->pcm_write || !io->stop_requested ||
       c->gain>128 || c->crop>1 || c->max_frame_skip>5 ||
       (!!io->video_begin != !!(io->video_rows || io->video_native_rows)) ||
       (io->video_rows && io->video_native_rows) ||
       (io->audio_buffered && !c->max_frame_skip) || c->x_offset>80 || c->y_offset>80)
        return FM1_NES_INVALID;
    for(i=0;i<8;++i)if(c->key_for_pad[i]>=FM1_KEY_COUNT &&
        c->key_for_pad[i]!=FM1_KEY_UNASSIGNED)return FM1_NES_INVALID;
    memset(b,0,sizeof(*b));b->io=*io;b->config=*c;
    b->platform.context=b;b->platform.video=video;b->platform.audio=audio;
    b->platform.buttons=buttons;b->platform.frame=frame;
    b->platform.render_frame=render_frame;
    if(io->audio_channels)b->platform.audio_channels=audio_channels;
    return 0;
}
int fm1_board_lcd_init(fm1_board *b) {
    unsigned i;
    if(b->io.delay_ms(b->io.context,100))return FM1_NES_IO_ERROR;
    for(i=0;i<21;++i) {
        if(i==1) {if(b->io.delay_ms(b->io.context,120))return FM1_NES_IO_ERROR;}
        else if(command(b,panel_init[i][0],panel_init[i]+2,panel_init[i][1]))
            return FM1_NES_IO_ERROR;
    }
    /* Visible startup checkpoint before audio/scanner/emulator can fail.
       Stock fills black before 29h; same byte count, now RGB565 blue. */
    for(i=0;i<sizeof(b->wire);i+=2){b->wire[i]=0;b->wire[i+1]=0x1f;}
    for(i=0;i<240;i+=FM1_LCD_STRIP_ROWS) {
        if(window(b,i,FM1_LCD_STRIP_ROWS) ||
           b->io.lcd_write(b->io.context,1,b->wire,sizeof(b->wire)))
            return FM1_NES_IO_ERROR;
    }
    return command(b,0x29,0,0);
}
int fm1_board_run(fm1_board *b,const uint8_t *rom,size_t size,uint32_t frames,fm1_nes_stats *stats) {
    int result;
    if(!b)return FM1_NES_INVALID;
    if(b->running)return FM1_NES_BUSY;
    result=fm1_nes_validate_rom(rom,size);if(result)return result;
    b->running=1;
    b->deadline=b->io.now_us(b->io.context);b->remainder=0;
    b->skip_video=b->skip_streak=0;b->presented_frames=b->skipped_frames=0;
    b->filter_started=b->fault=b->candidate=b->stable=0;
    memset(b->candidate_since,0,sizeof(b->candidate_since));
    result=fm1_nes_run(rom,size,&b->platform,frames,stats);
    b->running=0;
    return b->fault ? FM1_NES_IO_ERROR : result;
}
