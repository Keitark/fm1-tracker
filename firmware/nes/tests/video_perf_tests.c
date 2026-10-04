/* Exact-output comparison to the previous strip-window converter, plus an
   optional host-only timing benchmark. No device timing/FPS claim. */
#include "fm1_board.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static fm1_board board;
static uint16_t pixels[256*240];
static uint8_t capture[240*240*2],reference[240*240*2];
static unsigned bytes,writes,commands,fail_at,win_y0,win_y1,window_pixels;
static uint8_t cmd,xwin[4],ywin[4];
static volatile unsigned checksum;
#ifdef _MSC_VER
__declspec(noinline)
#endif
static int lcd(void *ctx,int data,const uint8_t *p,size_t n){
    (void)ctx;CHECK(p && n);if(++writes==fail_at)return -1;
    if(!data){
        CHECK(n==1);cmd=p[0];commands++;
        if(cmd==0x2c){
            CHECK(!window_pixels);
            win_y0=(ywin[0]<<8)|ywin[1];win_y1=(ywin[2]<<8)|ywin[3];
            CHECK(win_y1>=win_y0 && win_y1<320);
            CHECK(((xwin[2]<<8)|xwin[3])-((xwin[0]<<8)|xwin[1])==239);
            window_pixels=(win_y1-win_y0+1)*480;
        }
    } else if(cmd==0x2a){CHECK(n==4);memcpy(xwin,p,4);}
    else if(cmd==0x2b){CHECK(n==4);memcpy(ywin,p,4);}
    else {
        CHECK(cmd==0x2c && n<=3840 && n<=window_pixels && bytes+n<=sizeof(capture));
        memcpy(capture+bytes,p,n);bytes+=(unsigned)n;window_pixels-=(unsigned)n;
        checksum+=p[0]+p[n-1];
    }
    return 0;
}
static int delay(void *c,uint32_t n){(void)c;(void)n;return 0;}
static uint32_t now(void *c){(void)c;return 0;}
static uint64_t keys(void *c){(void)c;return 0;}
static int pcm(void *c,const int16_t *p,size_t n){(void)c;(void)p;(void)n;return 0;}
static int stop(void *c){(void)c;return 0;}
static int want_result,begin_wanted;
static int begin(void *c,int wanted){(void)c;begin_wanted=wanted;return want_result;}
static int async_rows(void *c,unsigned y,unsigned rows,const uint8_t *p){
    (void)c;CHECK(y*480==bytes && rows<=8);memcpy(capture+bytes,p,rows*480);bytes+=rows*480;return 0;
}
static unsigned native_y,native_crop,native_calls;
static int native_rows(void *c,unsigned y,unsigned rows,const uint16_t *p,unsigned crop){
    unsigned r,x;(void)c;CHECK(y==native_y && rows && rows<=8 && crop==native_crop);
    native_y+=rows;if(++native_calls==fail_at)return -1;
    for(r=0;r<rows;++r)for(x=0;x<240;++x){
        uint16_t v=p[r*256+(crop?x+8:x*256u/240u)];
        capture[bytes++]=(uint8_t)(v>>8);capture[bytes++]=(uint8_t)v;
    }
    return 0;
}
static void reset(void){bytes=writes=commands=window_pixels=fail_at=0;}
static void setup(unsigned crop,unsigned yoff){
    fm1_board_io io={0,lcd,delay,now,delay,keys,pcm,stop};fm1_board_config cfg;
    fm1_board_default_config(&cfg);cfg.crop=(uint8_t)crop;cfg.y_offset=(uint16_t)yoff;
    CHECK(!fm1_board_construct(&board,&io,&cfg));reset();
}
/* Previous algorithm, deliberately retaining per-pixel scaling and per-strip
   window commands. Run with the identical output callback and buffers. */
static int old_video(unsigned y,unsigned rows,const uint16_t *src){
    unsigned start,r,x,count;
    for(start=0;start<rows;start+=count){
        unsigned y0,y1;uint8_t command;
        uint8_t xs[]={0,0,0,239},ys[4];
        count=rows-start;if(count>8)count=8;
        for(r=0;r<count;r++)for(x=0;x<240;x++){
            unsigned index=board.config.crop?x+8:x*256u/240u;
            uint16_t p=src[(start+r)*256u+index];
            board.wire[(r*240+x)*2]=(uint8_t)(p>>8);board.wire[(r*240+x)*2+1]=(uint8_t)p;
        }
        y0=board.config.y_offset+y+start;y1=y0+count-1;
        ys[0]=(uint8_t)(y0>>8);ys[1]=(uint8_t)y0;ys[2]=(uint8_t)(y1>>8);ys[3]=(uint8_t)y1;
        command=0x2a;if(lcd(0,0,&command,1)||lcd(0,1,xs,4))return -3;
        command=0x2b;if(lcd(0,0,&command,1)||lcd(0,1,ys,4))return -3;
        command=0x2c;if(lcd(0,0,&command,1)||lcd(0,1,board.wire,count*480))return -3;
    }
    return 0;
}
static double measure(int old,unsigned frames){
    unsigned i;clock_t start=clock();
    for(i=0;i<frames;i++){
        reset();
        if(old){CHECK(!old_video(0,120,pixels));CHECK(!old_video(120,120,pixels+256*120));}
        else {CHECK(!board.platform.video(&board,0,120,pixels));CHECK(!board.platform.video(&board,120,120,pixels+256*120));}
    }
    return (double)(clock()-start)/CLOCKS_PER_SEC;
}
static int compare_double(const void *a,const void *b){double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
int main(int argc,char **argv){
    unsigned crop,y,rows,i;uint32_t random=1;
    for(i=0;i<256*240;i++){random=random*1664525u+1013904223u;pixels[i]=(uint16_t)(random>>8);}
    for(crop=0;crop<2;crop++)for(y=0;y<240;y+=17)for(rows=1;rows<=240-y;rows+=7){
        setup(crop,40);CHECK(!old_video(y,rows,pixels));memcpy(reference,capture,bytes);
        reset();CHECK(!board.platform.video(&board,y,rows,pixels));
        CHECK(bytes==rows*480 && !window_pixels && !memcmp(reference,capture,bytes));
        CHECK(commands==3 && writes==5+(rows+7)/8 && win_y0==y+40 && win_y1==y+40+rows-1);
    }
    setup(0,0);CHECK(!board.platform.video(&board,0,120,pixels));CHECK(!board.platform.video(&board,120,120,pixels+256*120));
    CHECK(bytes==115200 && writes==40 && commands==6 && !window_pixels);
    reset();CHECK(!old_video(0,120,pixels));CHECK(!old_video(120,120,pixels+256*120));
    CHECK(bytes==115200 && writes==180 && commands==90);
    for(i=1;i<=20;i++){reset();fail_at=i;CHECK(board.platform.video(&board,0,120,pixels)==FM1_NES_IO_ERROR && writes==i);}
    reset();board.skip_video=1;CHECK(!board.platform.video(&board,0,120,pixels) && !writes);
    CHECK(board.platform.video(&board,239,2,pixels)==FM1_NES_INVALID && !writes);
    setup(0,0);CHECK(!old_video(0,240,pixels));memcpy(reference,capture,bytes);reset();
    board.io.video_begin=begin;board.io.video_rows=async_rows;want_result=1;
    CHECK(board.platform.render_frame(&board)==1 && begin_wanted==1);
    CHECK(!board.platform.video(&board,0,120,pixels) && !board.platform.video(&board,120,120,pixels+256*120));
    CHECK(bytes==115200 && !commands && !writes && !memcmp(reference,capture,bytes));
    reset();want_result=0;CHECK(!board.platform.render_frame(&board));
    CHECK(!board.platform.video(&board,0,240,pixels) && !bytes);
    board.skip_video=1;CHECK(!board.platform.render_frame(&board) && !begin_wanted);
    want_result=-9;CHECK(!board.platform.render_frame(&board) && board.fault && board.platform.frame(&board,0)==1);
    /* Native callback preserves all pixels/order, including partial strips;
       no intermediate wire buffer and no retained core pointer. */
    for(crop=0;crop<2;++crop)for(y=0;y<240;y+=17)for(rows=1;rows<=240-y;rows+=7){
        setup(crop,0);CHECK(!old_video(y,rows,pixels));memcpy(reference,capture,bytes);reset();
        board.io.video_begin=begin;board.io.video_native_rows=native_rows;
        native_y=y;native_crop=crop;native_calls=0;
        memset(board.wire,0xa5,sizeof(board.wire));
        CHECK(!board.platform.video(&board,y,rows,pixels));
        CHECK(bytes==rows*480 && !commands && !writes && !memcmp(reference,capture,bytes));
        for(i=0;i<sizeof(board.wire);++i)CHECK(board.wire[i]==0xa5);
    }
    for(i=1;i<=15;++i){reset();native_y=0;native_calls=0;fail_at=i;
        CHECK(board.platform.video(&board,0,120,pixels)==FM1_NES_IO_ERROR && native_calls==i);}
    reset();native_calls=0;board.skip_video=1;
    CHECK(!board.platform.video(&board,0,120,pixels) && !native_calls);
    {fm1_board_io io=board.io;fm1_board_config cfg=board.config;
     CHECK(!fm1_board_construct(&board,&io,&cfg));
     io.video_rows=async_rows;CHECK(fm1_board_construct(&board,&io,&cfg)==FM1_NES_INVALID);
     io.video_rows=NULL;io.video_begin=NULL;CHECK(fm1_board_construct(&board,&io,&cfg)==FM1_NES_INVALID);}
    puts("PASS exact RGB565 stream, crop/scale/offset/partial strips, window bounds, transfer failures, skip; LCD calls/frame 180 -> 40, commands 90 -> 6");
    if(argc==2 && !strcmp(argv[1],"--bench")){
        double old[7],fast[7];setup(0,0);measure(0,20);measure(1,20);
        for(i=0;i<7;i++){
            if(i&1){fast[i]=measure(0,10000);old[i]=measure(1,10000);}
            else {old[i]=measure(1,10000);fast[i]=measure(0,10000);}
        }
        qsort(old,7,sizeof(double),compare_double);qsort(fast,7,sizeof(double),compare_double);
        printf("HOST ONLY 10000 frames medians: old=%.6fs new=%.6fs ratio=%.3fx checksum=%u\n",old[3],fast[3],old[3]/fast[3],checksum);
    }
    return 0;
}
