#include "display_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t regs[24],now;
static int frozen,stuck,invalid_clock,pending,cmd,commands[256],accesses,colmod;
static unsigned count;
#ifdef FM1_LCD_ASYNC
static void (*lcd_irq)(void);
static unsigned lcd_locked,complete_dma;
static unsigned complete_on_unlock;
static void complete_strip(void);
unsigned fm1_lcd_test_lock(void){CHECK(!lcd_locked);lcd_locked=1;return 0;}
void fm1_lcd_test_unlock(unsigned flags){
    CHECK(lcd_locked && flags==0);lcd_locked=0;
    if(complete_on_unlock && !--complete_on_unlock)complete_strip();
}
void fm1_lcd_test_irq_enable(void (*fn)(void)){CHECK(!lcd_irq);lcd_irq=fn;}
void fm1_lcd_test_irq_disable(void);
#endif
#ifdef FM1_NES_PLAYER
static unsigned stream_test;
#endif
static unsigned window_len[2];
static uint8_t window[2][4];
static int continuous;
static int reported_sys=192000000;
#ifdef FM1_LCD_FAST_SPI
static int reported_lsb=60000000;
#else
static int reported_lsb=48000000;
#endif
static unsigned delay_chunks,timer_reads,yields,clock_reads;
static unsigned baud_access_pending,baud_previous,baud_con_before,baud_writes;
#ifdef FM1_LCD_FAST_SPI
static unsigned baud_idle_before;
#endif
void fm1_early_delay_chunk(void){++delay_chunks;CHECK(regs[7]==4 && (regs[6]&1));}
static uint8_t pixels[240*480*2];
static const uint32_t addresses[]={0x50080,0x50088,0x5008c,0x50090,0x50094,0x51020,0x11d00,0x11d04,0x11d08,
    0x11d0c,0x11d10,0x50084,0x50098,0x5009c,0x500a0,0x5101c,0x51024,0x51028,0x51030,0x50000,0x50008};
#ifdef FM1_LCD_STOCK_SEQUENCE
static unsigned gpio_calls,delay100,delay120,pixel_dma;
static int fail_pixel_dma;
int gpio_direction_output(unsigned int pin,int value) {
    if(!gpio_calls){CHECK(pin==2 && value==0 && regs[5]==(1u<<25));regs[19]&=~4u;regs[20]&=~4u;}
    else {CHECK(gpio_calls==1 && pin==0x27 && value==1 && (regs[5]&16));regs[0]|=128;regs[1]&=~128u;}
    gpio_calls++;return 0;
}
int gpio_set_direction(unsigned int pin,unsigned int direction) {
    CHECK(gpio_calls>=2 && gpio_calls<=4 && pin==0x26+gpio_calls && direction==0);
    regs[1]&=~(1u<<(pin&15));gpio_calls++;return 0;
}
#endif
#ifdef FM1_LCD_STOCK_DMA
static const uint8_t *dma_source;
static unsigned dma_prepared,dma_pending,dma_transfers,dma_bytes,snapshot_mask;
static int stuck_dma,dma_blocked;
#ifdef FM1_LCD_ASYNC
void fm1_lcd_test_irq_disable(void){lcd_irq=NULL;if(regs[6]&0x2000)dma_pending=dma_prepared=0;}
#endif
void fm1_display_test_dma_source(const uint8_t *source){dma_source=source;dma_prepared=1;}
void fm1_display_snapshot(unsigned phase,const uint32_t values[FM1_LCD_REGISTER_COUNT]) {
    CHECK(phase<5);snapshot_mask|=1u<<phase;
    CHECK(values[0]==regs[0] && values[1]==regs[11] && values[2]==regs[1]);
    CHECK(values[10]==regs[5] && values[14]==regs[6] && values[15]==regs[7]);
    if(phase==1){CHECK((values[2]&0x780u)==0 && (values[10]&16) && values[15]==4);}
    if(phase==4)CHECK(values[14]==0);
}
#endif
extern volatile uint32_t fm1_display_frames;
static void data_byte(uint8_t value) {
    if(cmd==0x3a)colmod=value;
    if(cmd==0x2c){CHECK(count<sizeof(pixels));pixels[count++]=value;}
    else if(cmd==0x2a || cmd==0x2b){unsigned index=(unsigned)cmd-0x2a;CHECK(window_len[index]<4);window[index][window_len[index]++]=value;}
}
static void flush(void) {
    /* Register access returns an lvalue, so observe its write on the next
     * access. Snapshot reads leave BAUD unchanged and must not count as writes.
     * This enforces stock's sequence, not a claim about hardware reset behavior. */
    if(baud_access_pending) {
        baud_access_pending=0;
        if(regs[7]!=baud_previous) {
            CHECK((baud_con_before&1u)!=0);
#ifdef FM1_LCD_FAST_SPI
            CHECK(regs[7]==4 || (regs[7]==FM1_LCD_STREAM_BAUD && commands[0x29]==1 && baud_idle_before));
#else
            CHECK(regs[7]==4);
#endif
            ++baud_writes;
        }
    }
#ifdef FM1_LCD_STOCK_DMA
    if(dma_pending
#ifdef FM1_LCD_ASYNC
       && (!(regs[6]&0x2000) || complete_dma)
#endif
       ) {
        unsigned i;dma_pending=0;
        CHECK(!pending && dma_source && regs[10]>0);
#ifdef FM1_NES_PLAYER
        CHECK(regs[10]<=3840);
#ifdef FM1_LCD_ASYNC
        if(regs[6]&0x2000)CHECK(regs[10]==FM1_LCD_WIRE_STRIP_BYTES);
#endif
#else
        CHECK(regs[10]<=64);
#endif
        CHECK(!(regs[0]&(1u<<7)) && (regs[0]&(1u<<8)));
        CHECK(regs[9]==(uint32_t)(uintptr_t)dma_source);
        dma_blocked=stuck_dma;
#ifdef FM1_LCD_STOCK_SEQUENCE
        if(cmd==0x2c) {
#ifdef FM1_NES_PLAYER
            if(stream_test)CHECK(regs[10]<=3840);
            else
#endif
            CHECK(regs[10]==40 && commands[0x2c]<=2);
            if(fail_pixel_dma)dma_blocked=1;
            else pixel_dma++;
        }
#endif
        if(!dma_blocked) {
            for(i=0;i<regs[10];i++)data_byte(dma_source[i]);
            dma_transfers++;dma_bytes+=regs[10];
        }
    }
#endif
    if(!pending)return;
    pending=0;
    CHECK(!(regs[0]&(1u<<7)));
    if(!(regs[0]&(1u<<8))){cmd=regs[8]&255;++commands[cmd];
#ifdef FM1_LCD_STOCK_SEQUENCE
        CHECK(gpio_calls==5 && !(regs[19]&4) && !(regs[20]&4));
        if(cmd==0x11)CHECK(delay100==1 && delay120==0);
        if(cmd==0x2a)CHECK(delay120==1);
        if(cmd==0x29){unsigned i;CHECK(count==115200 && pixel_dma==2880);for(i=0;i<count;i++)CHECK(pixels[i]==0);}
#endif
        if(cmd==0x2a || cmd==0x2b)window_len[cmd-0x2a]=0;
    }
    else {
#ifdef FM1_LCD_STOCK_DMA
        CHECK(cmd==0x2c); /* Parameters must not use byte writes. */
#endif
        data_byte((uint8_t)regs[8]);
    }
}
volatile uint32_t *fm1_display_test_register(uint32_t address) {
    unsigned i;flush();++accesses;
#if defined(FM1_LCD_STOCK_FILL) && !defined(FM1_LCD_STOCK_SEQUENCE)
    if(cmd==0x2c && count%115200u) {CHECK(!(regs[0]&(1u<<7)) && (regs[0]&(1u<<8)));continuous++;}
#endif
    for(i=0;i<sizeof(addresses)/sizeof(addresses[0]);++i)if(address==addresses[i]) {
        if(i==6 && !stuck && (regs[i]&1)
#ifdef FM1_LCD_ASYNC
           && !(regs[i]&0x2000)
#endif
           )regs[i]|=0x8000;
#ifdef FM1_LCD_STOCK_DMA
        if(i==6 && dma_blocked)regs[i]&=~0x8000u;
        /* In production CNT is the final write. Snapshot reads must not
         * start DMA; recognize its use from the prepared source hook. */
        if(i==10 && dma_prepared){dma_pending=1;dma_prepared=0;}
#endif
        if(i==8)pending=1;
        if(i==7){baud_access_pending=1;baud_previous=regs[7];baud_con_before=regs[6];
#ifdef FM1_LCD_FAST_SPI
            baud_idle_before=!lcd_irq && !dma_pending;
#endif
        }
        return regs+i;
    }
    CHECK(0);return NULL;
}
uint32_t timer_get_ms(void){++timer_reads;return now;}
void os_time_dly(int ticks){
#ifdef FM1_LCD_STOCK_SEQUENCE
    if(ticks==10)delay100++;
    else if(ticks==12)delay120++;
    else {CHECK(ticks==1 && (regs[0]&128));}
#endif
#ifdef FM1_LCD_STOCK_DMA
    if(!yields){CHECK(regs[7]==4 && (regs[6]&1) && (snapshot_mask&2));}
#endif
    ++yields;if(!frozen)now+=(uint32_t)ticks*10;
}
void wdt_clear(void){}
int clk_get(const char *name){++clock_reads;return invalid_clock?0:(!strcmp(name,"sys")?reported_sys:reported_lsb);}
static void reset(void) {
    fm1_display_test_stop();memset(regs,0,sizeof(regs));memset(commands,0,sizeof(commands));
#ifdef FM1_LCD_STOCK_DMA
    dma_source=NULL;dma_prepared=dma_pending=dma_transfers=dma_bytes=snapshot_mask=0;stuck_dma=dma_blocked=0;
#endif
    now=count=0;frozen=stuck=invalid_clock=pending=accesses=0;
#ifdef FM1_NES_PLAYER
    stream_test=0;
#endif
    delay_chunks=timer_reads=yields=clock_reads=0;
    baud_access_pending=baud_previous=baud_con_before=baud_writes=0;
#ifdef FM1_LCD_STOCK_SEQUENCE
    gpio_calls=delay100=delay120=pixel_dma=0;fail_pixel_dma=0;
#endif
    continuous=0;memset(window_len,0,sizeof(window_len));memset(window,0,sizeof(window));
    cmd=colmod=0;
    regs[0]=1u<<25;regs[1]=regs[3]=regs[4]=0xffffffff;regs[5]=1u<<25;
    regs[19]=regs[20]=0xffffffff;
}
static unsigned color(unsigned frame,unsigned y) {
    unsigned off=(frame*240+y)*480;return pixels[off]*256u+pixels[off+1];
}
#ifdef FM1_LCD_ASYNC
static void complete_strip(void) {
    CHECK(lcd_irq && dma_pending && !lcd_locked && (regs[6]&0x2000));
    complete_dma=1;flush();complete_dma=0;
    regs[6]|=0x8000;lcd_irq();
}
static void async_fill(unsigned seed,unsigned first,unsigned end) {
    unsigned y,i;uint8_t row[3840];
    for(y=first;y<end;y+=8) {
        for(i=0;i<sizeof(row);++i)row[i]=(uint8_t)(seed+y*480u+i);
        CHECK(!fm1_display_async_rows(y,8,row));
        memset(row,0x55,sizeof(row)); /* Caller memory immediately reused. */
    }
}
static void check_async_frame(unsigned offset,unsigned seed) {
    unsigned i;
#ifdef FM1_LCD_RGB444
    /* Independent decoder: reconstruct 12-bit pixels, then compare each
     * channel with the original 565 component divided down to four bits. */
    for(i=0;i<240u*240u;++i) {
        unsigned n=offset+(i/2)*3u;
        unsigned decoded=(i&1)?((pixels[n+1]&15u)*256u+pixels[n+2]):(pixels[n]*16u+pixels[n+1]/16u);
        unsigned original=(uint8_t)(seed+2*i)*256u+(uint8_t)(seed+2*i+1);
        CHECK((decoded>>8)==((original>>11)/2));
        CHECK(((decoded>>4)&15)==(((original>>5)&63)/4));
        CHECK((decoded&15)==((original&31)/2));
    }
#else
    for(i=0;i<FM1_LCD_WIRE_FRAME_BYTES;++i)CHECK(pixels[offset+i]==(uint8_t)(seed+i));
#endif
}
#ifdef FM1_LCD_RGB444
static void test_rgb444_all_colors(void) {
    unsigned pass,y,i;uint8_t row[3840];
    reset();CHECK(!fm1_display_test_init() && colmod==0x55);stream_test=1;count=0;
    CHECK(!fm1_display_async_start() && colmod==0x53);
    /* 6 frames exercise all 65536 values in BOTH positions of a packed pair. */
    for(pass=0;pass<6;pass++) {
        count=0;CHECK(fm1_display_async_begin(1)==1);
        for(y=0;y<240;y+=8) {
            for(i=0;i<1920;i++) {
                unsigned value=((y*240+i)/2+(pass/2)*28800)&65535;
                if((i&1)!=(pass&1))value=0;
                row[2*i]=(uint8_t)(value>>8);row[2*i+1]=(uint8_t)value;
            }
            CHECK(!fm1_display_async_rows(y,8,row));
        }
        for(i=0;i<30;i++)complete_strip();
        CHECK(count==86400);
        for(i=0;i<57600;i++) {
            unsigned n=i/2*3;
            unsigned value=(i/2+(pass/2)*28800)&65535;
            unsigned got=(i&1)?((pixels[n+1]&15u)*256u+pixels[n+2]):(pixels[n]*16u+pixels[n+1]/16u);
            if((i&1)!=(pass&1))value=0;
            CHECK(got==((value>>12)*256+(((value>>5)&63)/4)*16+(value&31)/2));
        }
    }
    /* Failure of the format parameter must not enable asynchronous DMA. */
    reset();CHECK(!fm1_display_test_init() && colmod==0x55);stuck_dma=1;
    CHECK(fm1_display_async_start()!=0 && !lcd_irq && regs[6]==0);
    reset();CHECK(!fm1_display_test_init() && colmod==0x55);
    puts("PASS RGB444 pixel-pair packing, serial COLMOD, stock startup and mode-switch failure");
}
#endif
static void test_completion_race(void) {
    unsigned boundary,i,wanted;
    for(wanted=0;wanted<=1;++wanted)for(boundary=0;boundary<30;++boundary) {
        fm1_lcd_async_stats stats;
        reset();CHECK(!fm1_display_test_init());stream_test=1;count=0;
        CHECK(!fm1_display_async_start());
        CHECK(fm1_display_async_begin(1)==1);async_fill(17,0,240);
        CHECK(fm1_display_async_begin(1)==1);async_fill(99,0,240);
        for(i=0;i<boundary;++i)complete_strip();
        /* Real driver ISR at service unlock, before begin allocation lock.
           Final strip frees a buffer but leaves the older pending frame. */
        complete_on_unlock=1;
        CHECK(fm1_display_async_begin((int)wanted)==0);
        CHECK(!complete_on_unlock && !fm1_display_error && lcd_irq);
        for(i=boundary+1;i<30;++i)complete_strip();
        check_async_frame(0,17);count=0;
        CHECK(fm1_display_async_begin(1)==1);async_fill(201,0,240);
        for(i=0;i<30;++i)complete_strip();
        CHECK(count==FM1_LCD_WIRE_FRAME_BYTES);check_async_frame(0,99);count=0;
        CHECK(!fm1_display_async_service());
        for(i=0;i<30;++i)complete_strip();
        check_async_frame(0,201);
        fm1_display_async_snapshot(&stats);
        CHECK(stats.submitted==3 && stats.completed==3 && !stats.errors);
        CHECK(!stats.active && !stats.pending && !fm1_display_error);
    }
    puts("PASS 60 DMA completion/producer handoff cases; pending preserved, ordered frames, resumed building");
}
static void test_async(void) {
    fm1_lcd_async_stats stats;unsigned i,c;
    reset();CHECK(!fm1_display_test_init());stream_test=1;count=0;
    CHECK(!fm1_display_async_start());flush();CHECK(regs[7]==FM1_LCD_STREAM_BAUD);
    CHECK(colmod==(FM1_LCD_WIRE_BPP==12?0x53:0x55));
    CHECK(baud_writes==(FM1_LCD_STREAM_BAUD!=4?2u:1u));
    CHECK(fm1_display_async_start()==-4);
    CHECK(fm1_display_write(1,pixels,40)==-8 && fm1_display_test_frame(0)==-8);
    CHECK(fm1_display_async_begin(1)==1);
    async_fill(17,0,120);CHECK(!dma_pending && !count);
    async_fill(17,120,240);CHECK(dma_pending && !count);
    c=commands[0x2c];
    CHECK(fm1_display_async_begin(1)==1);async_fill(99,0,240);
    CHECK(commands[0x2c]==c && !count); /* No command/data collision. */
    CHECK(fm1_display_async_begin(1)==0);
    fm1_display_async_snapshot(&stats);
    CHECK(stats.submitted==2 && !stats.completed && stats.active==1 && stats.pending==1 && stats.busy_skips==1);
    for(i=0;i<30;i++)complete_strip();
    CHECK(count==FM1_LCD_WIRE_FRAME_BYTES && (regs[0]&128) && !(regs[6]&0x2000));
    check_async_frame(0,17);
    CHECK(fm1_display_async_begin(0)==0 && commands[0x2c]==c+1);
    CHECK(window[0][3]==239 && window[1][3]==239);
    /* Build the next picture while the second is actively transmitting. */
    CHECK(fm1_display_async_begin(1)==1);
    for(i=0;i<30;i++) {async_fill(201,i*8,(i+1)*8);complete_strip();}
    CHECK(count==2*FM1_LCD_WIRE_FRAME_BYTES);
    check_async_frame(FM1_LCD_WIRE_FRAME_BYTES,99);
    count=0;CHECK(!fm1_display_async_service());
    for(i=0;i<30;i++)complete_strip();
    CHECK(count==FM1_LCD_WIRE_FRAME_BYTES);check_async_frame(0,201);
    fm1_display_async_snapshot(&stats);
    CHECK(stats.submitted==3 && stats.completed==3 && stats.irqs==90 && stats.bytes==3*FM1_LCD_WIRE_FRAME_BYTES && !stats.errors);
    CHECK(!stats.active && !stats.pending);
    /* Missing completion cannot block the emulator: deadline faults/tears down. */
    CHECK(fm1_display_async_begin(1)==1);async_fill(1,0,240);
    now+=249;CHECK(!fm1_display_async_service());now++;
    CHECK(fm1_display_async_service()==-9 && fm1_display_error==-9);
    CHECK(!lcd_irq && !dma_pending && regs[6]==0 && (regs[0]&128));
    fm1_display_async_snapshot(&stats);CHECK(stats.errors==1 && !stats.active && !stats.pending);
    /* Bad row ordering fails before publication. Stop/restart masks IRQs. */
    reset();CHECK(!fm1_display_test_init());stream_test=1;
    CHECK(!fm1_display_async_start() && fm1_display_async_begin(1)==1);
    CHECK(fm1_display_async_rows(8,8,pixels)==-10 && !lcd_irq && regs[6]==0);
    reset();CHECK(!fm1_display_test_init());stream_test=1;count=0;
    CHECK(!fm1_display_async_start() && fm1_display_async_begin(1)==1);
    async_fill(1,0,240);fm1_display_test_stop();
    CHECK(!lcd_irq && !dma_pending && regs[6]==0);
#ifdef FM1_LCD_FAST_SPI
    /* A different LSB is rejected, not silently clocked above the test rate. */
    reset();CHECK(!fm1_display_test_init());CHECK(regs[7]==4);
    reported_lsb=80000000;CHECK(fm1_display_async_start()==-11);
    CHECK(!lcd_irq && regs[6]==0 && regs[7]==4 && fm1_display_error==-11);
    reported_lsb=60000000;
#endif
    puts("PASS async DMA ownership, copied snapshots, two-buffer backpressure, IRQ chain, command isolation, timeout and stop");
}
#endif
#ifdef FM1_LCD_DIRECT
static uint16_t native_pixels[256*240];
static void test_native_rows(void) {
    unsigned crop,pass,y,x,i;uint32_t random=123;
    /* Real async driver, full frame/half frame caller reuse, 30 IRQs, fit
       and crop. Expected mapping and unpacking are independent of packer. */
    for(crop=0;crop<2;++crop)for(pass=0;pass<3;++pass) {
        fm1_lcd_async_stats stats;
        reset();CHECK(!fm1_display_test_init());stream_test=1;count=0;
        CHECK(!fm1_display_async_start() && fm1_display_async_begin(1)==1);
        for(i=0;i<256*240;++i){random=random*1664525u+1013904223u;
            native_pixels[i]=pass==2?(uint16_t)(random>>8):(uint16_t)(i+pass*32768);}
        for(y=0;y<240;y+=8)CHECK(!fm1_display_async_native_rows(y,8,native_pixels+y*256,crop));
        CHECK(!count && dma_pending);
        /* Save expected packed nibbles into otherwise unused upper capture
           space, then destroy input before DMA starts reading the frame. */
        for(y=0;y<240;++y)for(x=0;x<240;++x) {
            unsigned v=native_pixels[y*256+(crop?x+8:x*256u/240u)];
            unsigned v12=(v>>12)*256+(((v>>5)&63)/4)*16+(v&31)/2;
            unsigned at=115200+2*(y*240+x);
            pixels[at]=(uint8_t)(v12>>8);pixels[at+1]=(uint8_t)v12;
        }
        memset(native_pixels,0xa5,sizeof(native_pixels));
        for(i=0;i<30;++i)complete_strip();CHECK(count==86400);
        for(i=0;i<57600;++i) {
            unsigned at=i/2*3;
            unsigned got=(i&1)?((pixels[at+1]&15)*256+pixels[at+2]):(pixels[at]*16+pixels[at+1]/16);
            CHECK(got==pixels[115200+2*i]*256u+pixels[115201+2*i]);
        }
        fm1_display_async_snapshot(&stats);CHECK(stats.completed==1 && stats.bytes==86400);
    }
    reset();CHECK(!fm1_display_test_init());stream_test=1;count=0;
    CHECK(!fm1_display_async_start() && fm1_display_async_begin(1)==1);
    CHECK(fm1_display_async_native_rows(0,8,native_pixels,2)==-10 && !lcd_irq && regs[6]==0);
    reset();CHECK(!fm1_display_test_init());stream_test=1;count=0;
    CHECK(!fm1_display_async_start() && fm1_display_async_begin(1)==1);
    CHECK(fm1_display_async_native_rows(8,8,native_pixels,0)==-10 && !lcd_irq && regs[6]==0);
    puts("PASS direct RGB444 fit/crop exact pixels, copied lifetime and guards");
}
#endif
int main(void) {
    reset();CHECK(fm1_display_test_init()==0);flush();
    CHECK(baud_writes==1);
    CHECK(count==240*480 && commands[0x29]==1 && commands[0x11]==1 && !commands[0x45]);
#ifdef FM1_LCD_STOCK_DMA
#ifdef FM1_LCD_STOCK_SEQUENCE
    CHECK(dma_transfers==2899 && dma_bytes==115262 && snapshot_mask==7 && pixel_dma==2880);
    CHECK(gpio_calls==5 && delay100==1 && delay120==1);
    CHECK(regs[3]==0xffffffff && regs[4]==0xffffffff && regs[2]==0);
    CHECK(regs[19]==0xfffffffbu && regs[20]==0xfffffffbu);
#else
    CHECK(dma_transfers==19 && dma_bytes==62 && snapshot_mask==7);
#endif
#endif
#ifdef FM1_LCD_STOCK_FILL
    {
        const uint8_t expected[]={0,0,0,0xf0};unsigned i;
        CHECK(commands[0x2a]==2 && commands[0x2b]==2 && commands[0x2c]==1);
        CHECK(window_len[0]==4 && window_len[1]==4);
        CHECK(!memcmp(window[0],expected,4) && !memcmp(window[1],expected,4));
#ifdef FM1_LCD_STOCK_SEQUENCE
        for(i=0;i<count;i++)CHECK(pixels[i]==0);
#else
        for(i=0;i<count;i++)CHECK(pixels[i]==0xff);
        CHECK(continuous>115200);
#endif
    }
#else
    CHECK(color(0,20)==0xf800 && color(0,100)==0x07e0 && color(0,200)==0x001f);
#ifdef FM1_BOOT_EARLY_DISPLAY
    CHECK(color(0,0)==0xffe0 && delay_chunks==220 && !timer_reads && !yields && !clock_reads);
#else
    CHECK(color(0,0)==0);
#endif
#endif
    CHECK((regs[0]&(1u<<25)) && (regs[5]&(1u<<25)) && regs[7]==4);
    CHECK(fm1_display_test_frame(21000)==0);flush();
    CHECK(color(1,0)==0xffff && color(1,239)==0xffff && fm1_display_frames==2);
#ifdef FM1_LCD_STOCK_FILL
    CHECK(commands[0x2a]==3 && commands[0x2b]==3 && commands[0x2c]==2 && count==230400);
#endif
    CHECK(fm1_display_test_init()==-4);
    fm1_display_test_stop();CHECK(regs[6]==0 && (regs[0]&(1u<<7)));
    CHECK(fm1_display_test_frame(0)==-4);
#ifdef FM1_BOOT_EARLY_DISPLAY
    reset();invalid_clock=frozen=1;CHECK(fm1_display_test_init()==0 && commands[0x29]==1);
    CHECK(!timer_reads && !yields && !clock_reads && delay_chunks==220);
#else
    reset();invalid_clock=1;CHECK(fm1_display_test_init()==-1 && accesses==0);
    reset();frozen=1;CHECK(fm1_display_test_init()==-3);
#ifdef FM1_LCD_STOCK_DMA
    CHECK(accesses>0 && regs[6]==0 && (snapshot_mask&8) && !commands[0x11]);
    reset();stuck_dma=1;CHECK(fm1_display_test_init()==-2);
    CHECK(fm1_display_error==-6 && regs[6]==0 && (regs[0]&(1u<<7)) && !commands[0x29]);
    CHECK(snapshot_mask&8);
#ifdef FM1_LCD_STOCK_SEQUENCE
    reset();fail_pixel_dma=1;CHECK(fm1_display_test_init()==-2);
    CHECK(fm1_display_error==-6 && count==0 && regs[6]==0 && !commands[0x29]);
    CHECK(snapshot_mask&8);
#endif
#else
    CHECK(accesses==0);
#endif
#endif
    reset();stuck=1;CHECK(fm1_display_test_init()==-2);CHECK(regs[6]==0 && !commands[0x29]);
    reset();reported_sys=480000000;CHECK(fm1_display_test_init()==0 && commands[0x29]==1);
#ifdef FM1_NES_PLAYER
    {
        uint8_t stream[3842],cmd2c=0x2c;unsigned i,before=dma_transfers;
        stream_test=1;count=0;
        for(i=0;i<sizeof(stream);i++)stream[i]=(uint8_t)i;
        CHECK(fm1_display_write(0,&cmd2c,1)==0);
        CHECK(fm1_display_write(1,stream,sizeof(stream))==0);flush();
        CHECK(dma_transfers==before+2 && count==sizeof(stream));
        CHECK(!memcmp(stream,pixels,sizeof(stream)) && (regs[0]&128));
        /* A full frame now takes 30 pixel DMAs, with byte-exact content. */
        count=0;before=dma_transfers;
        for(i=0;i<30;i++)CHECK(fm1_display_write(1,stream,3840)==0);
        flush();CHECK(count==115200 && dma_transfers==before+30);
        for(i=0;i<30;i++)CHECK(!memcmp(pixels+i*3840,stream,3840));
        CHECK(fm1_display_write(1,NULL,1)==-4);
        stuck_dma=1;CHECK(fm1_display_write(1,stream,3840)==-2 && fm1_display_error==-6 && regs[6]==0);
        CHECK(fm1_display_write(1,stream,40)==-4);
    }
#endif
#ifdef FM1_LCD_ASYNC
    test_async();
    test_completion_race();
#endif
#ifdef FM1_LCD_RGB444
    test_rgb444_all_colors();
#endif
#ifdef FM1_LCD_DIRECT
    test_native_rows();
#endif
    puts("display protocol/colors/guard tests passed");return 0;
}
