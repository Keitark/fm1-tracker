#include "fm1_board.h"
#include "fm1_stock_keys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {
    uint32_t now, waited, lcd_us;
    uint64_t keys;
    uint8_t command,commands[256],pixel[3840],window_x[4],window_y[4];
    unsigned command_count,writes,pixel_bytes,delay_count,delays[8],pcm_frames;
    int16_t minimum,maximum,last_pcm;
    int fail_lcd,fail_audio,fail_wait,stop;
} fake_io;
static fm1_board board;
static fm1_audio_queue queue;
static fake_io fake;
static uint16_t pixels[256*120];
static int lcd(void *c,int data,const uint8_t *p,size_t n) {
    fake_io *f=c;++f->writes;
    if(f->fail_lcd)return -1;
    if(!data){CHECK(n==1);f->command=p[0];
        if(f->command_count<256)f->commands[f->command_count]=p[0];
        ++f->command_count;
    } else if(f->command==0x2a){CHECK(n==4);memcpy(f->window_x,p,4);}
    else if(f->command==0x2b){CHECK(n==4);memcpy(f->window_y,p,4);}
    else if(f->command==0x2c){CHECK(n<=sizeof(f->pixel));memcpy(f->pixel,p,n);f->pixel_bytes+=(unsigned)n;f->now+=f->lcd_us;}
    return 0;
}
static int delay(void *c,uint32_t n){fake_io *f=c;CHECK(f->delay_count<8);f->delays[f->delay_count++]=n;f->now+=1000*n;return 0;}
static uint32_t now(void *c){return ((fake_io *)c)->now;}
static int wait_us(void *c,uint32_t n){fake_io *f=c;if(f->fail_wait)return -1;f->now+=n;f->waited+=n;return 0;}
static uint64_t keys(void *c){return ((fake_io *)c)->keys;}
static int stop(void *c){return ((fake_io *)c)->stop;}
static int pcm(void *c,const int16_t *p,size_t n){fake_io *f=c;size_t i;
    if(f->fail_audio)return -1;
    for(i=0;i<n;++i){if(p[i]<f->minimum)f->minimum=p[i];if(p[i]>f->maximum)f->maximum=p[i];f->last_pcm=p[i];}
    f->pcm_frames+=(unsigned)n;return 0;
}
static fm1_board_io io={&fake,lcd,delay,now,wait_us,keys,pcm,stop};
static fm1_board_config config;
static void setup(void){memset(&fake,0,sizeof(fake));fm1_board_default_config(&config);CHECK(fm1_board_construct(&board,&io,&config)==0);}

static void test_config(void){
    fm1_board_io invalid=io;
    setup();CHECK(fake.writes==0);CHECK(config.gain==0);
    CHECK(board.platform.buttons(&board)==0);
    config.key_for_pad[0]=41;CHECK(fm1_board_construct(&board,&io,&config)==FM1_NES_INVALID);
    fm1_board_default_config(&config);config.gain=129;CHECK(fm1_board_construct(&board,&io,&config)==FM1_NES_INVALID);
    fm1_board_default_config(&config);config.max_frame_skip=6;CHECK(fm1_board_construct(&board,&io,&config)==FM1_NES_INVALID);
    fm1_board_default_config(&config);invalid.now_us=0;CHECK(fm1_board_construct(&board,&invalid,&config)==FM1_NES_INVALID);
}
static void test_panel(void){
    unsigned i;
    setup();CHECK(fm1_board_lcd_init(&board)==0);
    CHECK(fake.delay_count==2 && fake.delays[0]==100 && fake.delays[1]==120);
    CHECK(fake.commands[0]==0x11 && fake.commands[1]==0x2a);
    for(i=0;i<fake.command_count;++i)CHECK(fake.commands[i]!=0x45);
    CHECK(fake.commands[fake.command_count-1]==0x29);
    CHECK(fake.pixel_bytes==240*240*2);
    for(i=0;i<sizeof(fake.pixel);++i)CHECK(fake.pixel[i]==((i&1u)?0x1f:0));
    setup();fake.fail_lcd=1;CHECK(fm1_board_lcd_init(&board)==FM1_NES_IO_ERROR);
    CHECK(fake.writes==1);
}
static void test_video(void){
    unsigned i,x;
    setup();for(i=0;i<256*120;++i)pixels[i]=(uint16_t)(0x8000u+(i%256u));
    CHECK(board.platform.video(&board,0,120,pixels)==0);
    CHECK(fake.pixel_bytes==240*120*2 && fake.command_count==3 && fake.writes==20);
    CHECK(fake.window_y[1]==0 && fake.window_y[3]==119);
    for(x=0;x<240;++x){CHECK(fake.pixel[x*2]==0x80);CHECK(fake.pixel[x*2+1]==x*256u/240u);}
    config.crop=1;config.y_offset=40;CHECK(fm1_board_construct(&board,&io,&config)==0);
    CHECK(board.platform.video(&board,238,2,pixels)==0);
    CHECK(fake.window_y[0]==1 && fake.window_y[1]==22 && fake.window_y[3]==23);
    for(x=0;x<240;++x)CHECK(fake.pixel[x*2+1]==x+8);
    CHECK(board.platform.video(&board,239,2,pixels)==FM1_NES_INVALID);
    CHECK(board.platform.video(&board,0,0,pixels)==FM1_NES_INVALID);
    fake.fail_lcd=1;CHECK(board.platform.video(&board,0,1,pixels)==FM1_NES_IO_ERROR);
}
static void test_audio(void){
    uint8_t sample[1024];unsigned i;
    setup();for(i=0;i<sizeof(sample);++i)sample[i]=(uint8_t)i;
    CHECK(board.platform.audio(&board,sample,sizeof(sample))==0);
    CHECK(fake.minimum==0 && fake.maximum==0 && fake.pcm_frames==1024);
    config.gain=128;CHECK(fm1_board_construct(&board,&io,&config)==0);
    memset(sample,0,sizeof(sample));CHECK(board.platform.audio(&board,sample,sizeof(sample))==0);
    memset(sample,255,sizeof(sample));
    for(i=0;i<8;++i)CHECK(board.platform.audio(&board,sample,sizeof(sample))==0);
    CHECK(fake.maximum>30000 && fake.maximum<=32640 && abs(fake.last_pcm)<=128);
    memset(sample,0,sizeof(sample));CHECK(board.platform.audio(&board,sample,sizeof(sample))==0);
    CHECK(fake.minimum<0 && fake.minimum>=-32640);
    fake.fail_audio=1;CHECK(board.platform.audio(&board,sample,1)==FM1_NES_IO_ERROR);
    /* NES CDC gain4 raises quiet gameplay without approaching clipping. */
    setup();config.gain=4;CHECK(!fm1_board_construct(&board,&io,&config));
    memset(sample,0,sizeof(sample));CHECK(!board.platform.audio(&board,sample,sizeof(sample)));
    memset(sample,255,sizeof(sample));CHECK(!board.platform.audio(&board,sample,sizeof(sample)));
    CHECK(fake.maximum>900 && fake.maximum<=1020);
    memset(sample,0,sizeof(sample));CHECK(!board.platform.audio(&board,sample,sizeof(sample)));
    CHECK(fake.minimum<0 && fake.minimum>=-1020);
}
static void test_keys(void){
    unsigned i;
    setup();for(i=0;i<8;++i)config.key_for_pad[i]=(uint8_t)i;
    CHECK(fm1_board_construct(&board,&io,&config)==0);
    fake.keys=1;CHECK(board.platform.buttons(&board)==0);
    fake.now=9000;CHECK(board.platform.buttons(&board)==0);
    fake.keys=0;CHECK(board.platform.buttons(&board)==0);
    fake.now=10000;fake.keys=1;CHECK(board.platform.buttons(&board)==0);
    fake.now=20000;CHECK(board.platform.buttons(&board)==1);
    fake.keys=255;CHECK(board.platform.buttons(&board)==1);
    fake.now=30000;CHECK(board.platform.buttons(&board)==15); /* neutral opposites */
    fake.keys=0;board.platform.buttons(&board);fake.now=40000;CHECK(board.platform.buttons(&board)==0);
    fake.now=UINT32_MAX-5000;fake.keys=2;board.platform.buttons(&board);
    fake.now=5000;CHECK(board.platform.buttons(&board)==2);
    config.key_for_pad[0]=40;CHECK(fm1_board_construct(&board,&io,&config)==0);
    fake.keys=(uint64_t)1<<40;board.platform.buttons(&board);fake.now+=10000;
    CHECK(board.platform.buttons(&board)==1);
}
static void test_time(void){
    unsigned i;
    setup();for(i=1;i<=60;++i)CHECK(board.platform.frame(&board,i)==0);
    CHECK(fake.now==1000000 && fake.waited==1000000);
    setup();fake.now=UINT32_MAX-100;board.deadline=fake.now;
    CHECK(board.platform.frame(&board,1)==0);CHECK(fake.waited==16666);
    fake.now+=100000;CHECK(board.platform.frame(&board,2)==0);CHECK(board.deadline==fake.now);
    fake.fail_wait=1;CHECK(board.platform.frame(&board,3)!=0 && board.fault);
    setup();fake.stop=1;CHECK(board.platform.frame(&board,1)!=0 && fake.waited==0);
}
static void setup_skip(void){
    setup();config.max_frame_skip=5;config.gain=4;config.key_for_pad[0]=0;
    CHECK(!fm1_board_construct(&board,&io,&config));
}
static void test_auto_skip(void){
    unsigned i;uint8_t samples[]={0,255};
    setup_skip();
    for(i=1;i<=60;i++)CHECK(!board.platform.frame(&board,i));
    CHECK(!board.skipped_frames && board.presented_frames==60 && fake.now==1000000);
    setup_skip();fake.now=60000;CHECK(!board.platform.frame(&board,1) && board.skip_video);
    CHECK(!board.platform.video(&board,0,120,pixels));
    CHECK(!board.platform.video(&board,120,120,pixels));
    CHECK(!fake.writes && !fake.pixel_bytes); /* Neither half reaches LCD. */
    CHECK(board.platform.video(&board,239,2,pixels)==FM1_NES_INVALID);
    CHECK(!board.platform.audio(&board,samples,2) && fake.pcm_frames==2);
    fake.keys=1;CHECK(!board.platform.buttons(&board));
    for(i=2;i<=4;i++){fake.now+=5000;CHECK(!board.platform.frame(&board,i));}
    CHECK(!board.skip_video && board.skipped_frames==3 && board.presented_frames==1);
    CHECK(board.platform.buttons(&board)==FM1_PAD_A); /* Input not suppressed. */
    CHECK(!board.platform.video(&board,0,120,pixels) && fake.pixel_bytes==240*120*2);
    setup_skip();
    for(i=1;i<=6;i++){
        fake.now+=1000000;CHECK(!board.platform.frame(&board,i));
        CHECK(fake.now-board.deadline==100000); /* Bounded debt. */
    }
    CHECK(!board.skip_video && board.skipped_frames==5 && board.presented_frames==1);
    fake.now+=1000000;CHECK(!board.platform.frame(&board,7) && board.skip_video);
    CHECK(board.presented_frames==2);
    fake.stop=1;CHECK(board.platform.frame(&board,8)!=0 && !fake.waited);
    setup_skip();fake.now=UINT32_MAX-100;board.deadline=fake.now;
    fake.now+=60000;CHECK(!board.platform.frame(&board,1) && board.skip_video);
    setup_skip();fake.fail_wait=1;CHECK(board.platform.frame(&board,1)!=0 && board.fault);
}
static void test_note_keys(void){
    /* Deliberately scrambled fake scanner IDs, including slot 40. These are
       test fixtures, not FM-1 GPIO/physical mappings. */
    uint8_t slots[12]={40,5,21,4,30,255,15,7,255,2,255,255};
    const uint8_t expected[8]={2,7,5,15,4,21,40,30};
    unsigned i;
    setup();config.gain=3;config.crop=1;config.y_offset=40;
    CHECK(fm1_board_assign_note_keys(&config,slots)==0);
    CHECK(memcmp(config.key_for_pad,expected,8)==0);
    CHECK(config.gain==3 && config.crop==1 && config.y_offset==40 && fake.writes==0);
    for(i=0;i<8;++i){
        CHECK(fm1_board_construct(&board,&io,&config)==0);
        fake.keys=(uint64_t)1<<expected[i];
        CHECK(board.platform.buttons(&board)==0);
        fake.now+=10000;CHECK(board.platform.buttons(&board)==(uint8_t)(1u<<i));
        fake.keys=0;board.platform.buttons(&board);fake.now+=10000;
        CHECK(board.platform.buttons(&board)==0);
    }
    /* Simultaneous Right+A, then opposite directions, use the normal debouncer. */
    fake.keys=((uint64_t)1<<slots[4])|((uint64_t)1<<slots[9]);
    board.platform.buttons(&board);fake.now+=10000;
    CHECK(board.platform.buttons(&board)==(FM1_PAD_RIGHT|FM1_PAD_A));
    fake.keys|=(uint64_t)1<<slots[0];board.platform.buttons(&board);fake.now+=10000;
    CHECK(board.platform.buttons(&board)==FM1_PAD_A);
    slots[9]=255;CHECK(fm1_board_assign_note_keys(&config,slots)==FM1_NES_BSP_UNVERIFIED);
    CHECK(memcmp(config.key_for_pad,expected,8)==0);
    slots[9]=41;CHECK(fm1_board_assign_note_keys(&config,slots)==FM1_NES_INVALID);
    CHECK(memcmp(config.key_for_pad,expected,8)==0);
    slots[9]=slots[0];CHECK(fm1_board_assign_note_keys(&config,slots)==FM1_NES_INVALID);
    CHECK(memcmp(config.key_for_pad,expected,8)==0);
    CHECK(fm1_board_assign_note_keys(&config,0)==FM1_NES_INVALID);
    CHECK(fm1_board_assign_note_keys(0,slots)==FM1_NES_INVALID);
}
static void test_queue(void){
    int16_t input[FM1_AUDIO_CAPACITY],output[256];unsigned i;
    fm1_audio_queue_reset(&queue);
    for(i=0;i<FM1_AUDIO_CAPACITY;++i)input[i]=(int16_t)(i-2048);
    queue.write_pos=queue.read_pos=UINT32_MAX-100;
    CHECK(fm1_audio_queue_push(&queue,input,FM1_AUDIO_CAPACITY)==0);
    CHECK(fm1_audio_queue_push(&queue,input,1)==FM1_NES_BUSY);
    for(i=0;i<FM1_AUDIO_CAPACITY;++i){fm1_audio_queue_stereo(&queue,output,1);CHECK(output[0]==input[i] && output[1]==input[i]);}
    memset(output,0x7f,sizeof(output));fm1_audio_queue_stereo(&queue,output,128);
    for(i=0;i<256;++i)CHECK(output[i]==0);
    CHECK(queue.underrun_frames==128);
    CHECK(fm1_audio_queue_push(&queue,input,FM1_AUDIO_CAPACITY+1)==FM1_NES_INVALID);
}
static void test_queue24(void){
    static const int16_t samples[]={-32768,-12345,-1,0,1,12345,32767};
    static const int32_t expected[]={-8388608,-3160320,-256,0,256,3160320,8388352};
    fm1_audio_queue q;int32_t out[18];unsigned i;
    fm1_audio_queue_reset(&q);q.read_pos=q.write_pos=UINT32_MAX-2;
    CHECK(fm1_audio_queue_push(&q,samples,7)==0);
    out[0]=out[17]=0x12345678;
    fm1_audio_queue_stereo24(&q,out+1,8);
    CHECK(out[0]==0x12345678 && out[17]==0x12345678);
    for(i=0;i<7;++i)CHECK(out[1+i*2]==expected[i] && out[2+i*2]==expected[i]);
    CHECK(out[15]==0 && out[16]==0 && q.underrun_frames==1 && q.read_pos==q.write_pos);
}
static void test_audio_startup(void){
    fm1_audio_startup state;int32_t out[130];unsigned block,i;
    fm1_audio_startup_reset(&state);
    CHECK(state.frames_left==44118 && state.gain_q7==0);
    out[0]=out[129]=0x12345678;
    for(block=0;block<690;++block){
        for(i=1;i<=128;++i)out[i]=(i&1)?-8388608:8388607;
        fm1_audio_startup_process24(&state,out+1);
        for(i=1;i<=128;++i)CHECK(out[i]==0);
        CHECK(state.gain_q7==0 && out[0]==0x12345678 && out[129]==0x12345678);
    }
    CHECK(state.frames_left==0);
    for(block=1;block<=140;++block){
        unsigned gain=block>127?127:block;
        for(i=1;i<=128;++i)out[i]=(i&1)?-8388608:8388607;
        fm1_audio_startup_process24(&state,out+1);
        CHECK(state.gain_q7==gain && state.frames_left==0);
        for(i=1;i<=128;++i)CHECK(out[i]==((i&1)?-(int32_t)(65536*gain):(int32_t)(65536*gain-1)));
    }
    out[1]=-1;out[2]=1;out[3]=0;
    fm1_audio_startup_process24(&state,out+1);
    CHECK(out[1]==-1 && out[2]==0 && out[3]==0); /* signed rounding */
    state.target_q7=0;
    for(block=0;block<127;block++){
        for(i=1;i<=128;i++)out[i]=8388607;
        fm1_audio_startup_process24(&state,out+1);
        CHECK(state.gain_q7==126-block);
    }
    CHECK(out[1]==0);
    state.target_q7=64;
    for(block=0;block<64;block++)fm1_audio_startup_process24(&state,out+1);
    CHECK(state.gain_q7==64);
    fm1_audio_startup_reset(&state);out[1]=8388607;
    fm1_audio_startup_process24(&state,out+1);
    CHECK(out[1]==0 && state.frames_left==44054 && state.gain_q7==0);
    CHECK(out[0]==0x12345678 && out[129]==0x12345678);
}
static void test_stock_keys(void){
    /* Independent row/column vectors transcribed from the reviewed stock table. */
    static const uint8_t rows_for_slot[]={1,2,7,5,3,1,10,9,8,6,4,2,0,8,
        4,3,6,5,8,7,9,10,0,1,2,3,4,5,6,7,8,9,10,0,1,2,3,4,5,7,6};
    static const uint8_t cols_for_slot[]={4,4,1,1,1,1,2,2,1,1,1,1,2,2,
        4,4,4,4,4,4,4,4,4,3,3,3,3,3,3,3,3,3,3,3,2,2,2,2,2,2,2};
    static const uint8_t pad_slots[]={40,38,19,22,17,16,14,18};
    uint8_t rows[11];unsigned i,r,col;fm1_board_config c;
    CHECK(fm1_stock_decode_keys(0)==0);
    memset(rows,0xff,sizeof(rows));CHECK(fm1_stock_decode_keys(rows)==0);
    for(r=0;r<11;++r)for(col=0;col<6;++col){
        uint64_t expected=0;memset(rows,0xff,sizeof(rows));rows[r]&=(uint8_t)~(1u<<col);
        for(i=0;i<41;++i)if(rows_for_slot[i]==r && cols_for_slot[i]==col)expected|=UINT64_C(1)<<i;
        CHECK(fm1_stock_decode_keys(rows)==expected);
    }
    memset(rows,0xc0,sizeof(rows));CHECK(fm1_stock_decode_keys(rows)==((UINT64_C(1)<<41)-1));
    for(i=0;i<64;++i){
        uint32_t a=(i&1u)|((i&0x1eu)<<4),b=(i&0x20u)<<2;
        CHECK(fm1_stock_pack_columns(a,b)==i);
        CHECK(fm1_stock_pack_columns(a|0xfffffe1eu,b|0xffffff7fu)==i);
    }
    fm1_board_default_config(&c);c.gain=7;c.x_offset=8;
    CHECK(fm1_stock_assign_note_keys(&c)==0 && c.gain==7 && c.x_offset==8);
    CHECK(memcmp(c.key_for_pad,pad_slots,8)==0);
    CHECK(fm1_stock_assign_note_keys(0)==FM1_NES_INVALID);
    setup();CHECK(fm1_stock_assign_note_keys(&config)==0);
    for(i=0;i<41;++i){
        unsigned slot=i,j,expected=0;
        for(j=0;j<8;++j)if(pad_slots[j]==slot)expected|=1u<<j;
        CHECK(fm1_board_construct(&board,&io,&config)==0);
        memset(rows,0xff,sizeof(rows));rows[rows_for_slot[slot]]&=(uint8_t)~(1u<<cols_for_slot[slot]);
        fake.keys=fm1_stock_decode_keys(rows);
        CHECK(board.platform.buttons(&board)==0);fake.now+=10000;
        CHECK(board.platform.buttons(&board)==expected);
        memset(rows,0xff,sizeof(rows));fake.keys=fm1_stock_decode_keys(rows);
        board.platform.buttons(&board);fake.now+=10000;CHECK(board.platform.buttons(&board)==0);
    }
    /* Far-right A+B can stay held with a left-hand direction. In particular
       slot40 must not be truncated by a 32-bit input mask. */
    CHECK(fm1_board_construct(&board,&io,&config)==0);
    fake.keys=(UINT64_C(1)<<40)|(UINT64_C(1)<<38)|(UINT64_C(1)<<18);
    board.platform.buttons(&board);fake.now+=10000;
    CHECK(board.platform.buttons(&board)==(FM1_PAD_RIGHT|FM1_PAD_A|FM1_PAD_B));
    fake.now+=1000000;
    CHECK(board.platform.buttons(&board)==(FM1_PAD_RIGHT|FM1_PAD_A|FM1_PAD_B));
    fake.keys|=UINT64_C(1)<<14;board.platform.buttons(&board);fake.now+=10000;
    CHECK(board.platform.buttons(&board)==(FM1_PAD_A|FM1_PAD_B));
    fake.keys=0;board.platform.buttons(&board);fake.now+=10000;
    CHECK(board.platform.buttons(&board)==0);
}
static void test_integration(const char *path){
    const uint8_t measured_notes[12]={10,11,12,13,14,15,16,17,18,0,19,20}; /* fake */
    uint8_t *rom;long size;FILE *f=fopen(path,"rb");fm1_nes_stats stats;
    CHECK(f);CHECK(fseek(f,0,SEEK_END)==0);size=ftell(f);CHECK(size>0);rewind(f);
    rom=malloc((size_t)size);CHECK(rom);CHECK(fread(rom,1,(size_t)size,f)==(size_t)size);fclose(f);
    setup();CHECK(fm1_board_assign_note_keys(&config,measured_notes)==0);
    config.gain=16;CHECK(fm1_board_construct(&board,&io,&config)==0);
    fake.keys=1;
    CHECK(fm1_board_run(&board,rom,(size_t)size,12,&stats)==0);
    CHECK(stats.frames==12 && stats.ram0==0x5a && stats.ram1==1);
    CHECK(fake.pcm_frames==12*735 && fake.pixel_bytes==12*240*240*2 && fake.now==200000);
    CHECK(fake.maximum>fake.minimum);
    /* Artificial 60ms LCD cost: all emulation/audio frames still execute. */
    setup_skip();fake.lcd_us=2000;
    CHECK(!fm1_board_run(&board,rom,(size_t)size,12,&stats));
    CHECK(stats.frames==12 && stats.ram0==0x5a && fake.pcm_frames==12*735);
    CHECK(board.skipped_frames>0 && board.presented_frames+board.skipped_frames==12);
    CHECK(fake.pixel_bytes==board.presented_frames*240*240*2);
    setup();fake.fail_wait=1;CHECK(fm1_board_run(&board,rom,(size_t)size,12,&stats)==FM1_NES_IO_ERROR);
    setup();fake.fail_audio=1;CHECK(fm1_board_run(&board,rom,(size_t)size,12,&stats)==FM1_NES_IO_ERROR);
    setup();fake.fail_lcd=1;CHECK(fm1_board_run(&board,rom,(size_t)size,12,&stats)==FM1_NES_IO_ERROR);
    free(rom);
}
int main(int argc,char **argv){
    CHECK(argc==2);test_config();test_panel();test_video();test_audio();test_keys();test_note_keys();test_time();test_auto_skip();test_queue();test_queue24();test_audio_startup();test_stock_keys();test_integration(argv[1]);
    puts("PASS: configuration, recovered LCD script, video, audio, keys, piano-note preset, timing, queue, end-to-end NES board pipeline");return 0;
}
