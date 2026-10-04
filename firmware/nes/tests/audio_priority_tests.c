#include "fm1_board.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)

static void consumer_equivalence(void) {
    fm1_audio_queue q,reference;fm1_audio_startup s,r;
    int16_t input[FM1_AUDIO_PRIME];int32_t out[130],expected[128];unsigned i,block;
    fm1_audio_queue_reset(&q);fm1_audio_startup_reset(&s);
    q.read_pos=q.write_pos=UINT32_MAX-31u;
    for(i=0;i<FM1_AUDIO_PRIME;++i)input[i]=(int16_t)(i*177u);
    CHECK(!fm1_audio_queue_push(&q,input,FM1_AUDIO_PRIME));reference=q;r=s;
    out[0]=out[129]=0x12345678;
    for(block=0;block<2200;++block) {
        fm1_audio_queue_play24(&q,&s,out+1);
        fm1_audio_queue_stereo24(&reference,expected,64);
        fm1_audio_startup_process24(&r,expected);
        CHECK(!memcmp(out+1,expected,sizeof(expected)));
        CHECK(q.read_pos==reference.read_pos && s.gain_q7==r.gain_q7 && s.frames_left==r.frames_left);
        CHECK(out[0]==0x12345678 && out[129]==0x12345678);
        /* Includes every signed16 value and both extrema after the ramp. */
        for(i=0;i<64;++i)input[i]=(int16_t)((block*64u+i)&65535u);
        CHECK(!fm1_audio_queue_push(&q,input,64));CHECK(!fm1_audio_queue_push(&reference,input,64));
    }
    CHECK(!q.underrun_frames && !q.rebuffer_events && q.primed);
}
static void prefill_and_recovery(void) {
    fm1_audio_queue q;fm1_audio_startup s;int16_t in[FM1_AUDIO_PRIME];int32_t out[128];unsigned i;
    fm1_audio_queue_reset(&q);fm1_audio_startup_reset(&s);s.frames_left=0;s.gain_q7=127;
    for(i=0;i<FM1_AUDIO_PRIME;++i)in[i]=(int16_t)(i+1);
    CHECK(!fm1_audio_queue_push(&q,in,FM1_AUDIO_PRIME-1));
    fm1_audio_queue_play24(&q,&s,out);CHECK(!q.read_pos && !q.primed && q.priming_frames==64 && !q.underrun_frames);
    for(i=0;i<128;++i)CHECK(out[i]==0);
    CHECK(!fm1_audio_queue_push(&q,in+FM1_AUDIO_PRIME-1,1));
    for(i=0;i<22;++i)fm1_audio_queue_play24(&q,&s,out);
    CHECK(q.primed && q.read_pos==1408 && !q.underrun_frames);
    fm1_audio_queue_play24(&q,&s,out); /* Last62 valid frames, two missing. */
    CHECK(out[0]==1409*254 && out[122]==1470*254 && !out[124] && !out[126]);
    CHECK(q.read_pos==q.write_pos && !q.primed && q.rebuffer_events==1 && q.underrun_frames==2);
    CHECK(!fm1_audio_queue_push(&q,in,64));
    fm1_audio_queue_play24(&q,&s,out);CHECK(q.read_pos==1470 && q.underrun_frames==66);
    CHECK(!fm1_audio_queue_push(&q,in+64,FM1_AUDIO_PRIME-64));
    fm1_audio_queue_play24(&q,&s,out);CHECK(q.primed && out[0]==254 && out[126]==64*254);
    CHECK(q.rebuffer_events==1 && q.priming_frames==64);
    fm1_audio_queue_reset(&q);CHECK(!q.primed && !q.rebuffer_events && !q.priming_frames);
}

typedef struct {
    fm1_audio_queue queue;fm1_audio_startup envelope;
    uint32_t now,fraction,to_irq,waited,depth;
    unsigned simulate,fail_wait,stop,blocks,frames,pcm,shown;
} model;
static model m;static fm1_board b;static uint16_t pixels[256*120];
static void advance(uint32_t us) {
    uint64_t clocks=(uint64_t)us*44100u+m.fraction;
    uint32_t frames=(uint32_t)(clocks/1000000u);int32_t out[128];
    m.now+=us;m.fraction=(uint32_t)(clocks%1000000u);
    if(!m.simulate)return;
    m.to_irq+=frames;
    while(m.to_irq>=64) {m.to_irq-=64;fm1_audio_queue_play24(&m.queue,&m.envelope,out);}
}
static int lcd(void *ctx,int data,const uint8_t *p,size_t n) {
    (void)ctx;(void)p;if(data && n>4){m.blocks++;advance(40000u/30u);}return 0;
}
static int delay(void *ctx,uint32_t n){(void)ctx;advance(n*1000u);return 0;}
static uint32_t now(void *ctx){(void)ctx;return m.now;}
static int wait_us(void *ctx,uint32_t n){(void)ctx;if(m.fail_wait)return -1;m.waited+=n;advance(((n+9999u)/10000u)*10000u);return 0;}
static uint64_t keys(void *ctx){(void)ctx;return 0;}
static int pcm(void *ctx,const int16_t *p,size_t n){(void)ctx;m.pcm+=(unsigned)n;return fm1_audio_queue_push(&m.queue,p,n);}
static int stop(void *ctx){(void)ctx;return m.stop;}
static uint32_t buffered(void *ctx){(void)ctx;return m.simulate?m.queue.write_pos-m.queue.read_pos:m.depth;}
static void setup(int priority) {
    fm1_board_config c;fm1_board_io io={0,lcd,delay,now,wait_us,keys,pcm,stop,0};
    memset(&m,0,sizeof(m));fm1_audio_startup_reset(&m.envelope);
    fm1_board_default_config(&c);c.gain=4;
    if(priority){io.audio_buffered=buffered;c.max_frame_skip=5;}
    CHECK(!fm1_board_construct(&b,&io,&c));
}
static void pacing(void) {
    unsigned i;setup(1);
    m.now=UINT32_MAX-5000u;m.depth=0;
    CHECK(!b.platform.frame(&b,1) && b.skip_video && !m.waited);
    for(i=0;i<4;++i)CHECK(!b.platform.frame(&b,i+2) && b.skip_video);
    CHECK(!b.platform.frame(&b,6) && !b.skip_video && b.skip_streak==5);
    m.depth=FM1_AUDIO_TARGET;CHECK(!b.platform.frame(&b,7) && !b.skip_video && !m.waited);
    m.depth=FM1_AUDIO_TARGET+440;CHECK(!b.platform.frame(&b,8) && !m.waited);
    m.depth=FM1_AUDIO_TARGET+441;CHECK(!b.platform.frame(&b,9) && m.waited==10000);
    m.depth=FM1_AUDIO_CAPACITY;CHECK(!b.platform.frame(&b,10) && m.waited==30000);
    m.fail_wait=1;CHECK(b.platform.frame(&b,11) && b.fault);
    setup(1);m.stop=1;m.depth=FM1_AUDIO_CAPACITY;CHECK(b.platform.frame(&b,1) && !m.waited);
}
static uint32_t simulation(int priority,int stall) {
    unsigned i;uint8_t samples[735];uint32_t underruns;
    setup(priority);m.simulate=1;for(i=0;i<735;++i)samples[i]=(uint8_t)i;
    for(i=0;i<600;++i) {
        advance(2000); /* CPU/APU cost; LCD costs40ms per displayed frame. */
        CHECK(!b.platform.video(&b,0,120,pixels));CHECK(!b.platform.video(&b,120,120,pixels));
        CHECK(!b.platform.audio(&b,samples,735));
        if(stall && i==100)advance(150000);
        CHECK(!b.platform.frame(&b,i+1));
        CHECK(m.queue.write_pos-m.queue.read_pos<=FM1_AUDIO_CAPACITY);
        CHECK(b.skip_streak<=5);
    }
    CHECK(m.pcm==600*735 && b.presented_frames+b.skipped_frames==600);
    CHECK(m.blocks==b.presented_frames*30);
    underruns=m.queue.underrun_frames;
    printf("MODEL priority=%d injected150msStall=%d: underrun=%u rebuffer=%u shown=%u skipped=%u wall_us=%u\n",
        priority,stall,underruns,m.queue.rebuffer_events,b.presented_frames,b.skipped_frames,m.now);
    if(priority && !stall)CHECK(!underruns && !m.queue.rebuffer_events && b.skipped_frames>0);
    if(stall)CHECK(underruns && m.queue.rebuffer_events);
    return underruns;
}
int main(void) {
    consumer_equivalence();prefill_and_recovery();pacing();
    CHECK(simulation(0,0)>0);CHECK(!simulation(1,0));simulation(1,1);
    puts("PASS audio priority: exact conversion, prefill, bounded recovery/pacing and slow-LCD model");return 0;
}
