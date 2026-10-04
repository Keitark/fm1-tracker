#include "peripheral_logic.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}} while(0)
static void contact(uint8_t rows[11],unsigned index,unsigned value) {
    unsigned a=fm1_encoder_contacts[2*index],b=fm1_encoder_contacts[2*index+1];
    rows[a>>4]=(rows[a>>4]|(1u<<(a&15)))&~((value&1)<<(a&15));
    rows[b>>4]=(rows[b>>4]|(1u<<(b&15)))&~(((value>>1)&1)<<(b&15));
}
static void stable(fm1_encoders *s,uint8_t rows[11],unsigned i,unsigned value) {
    contact(rows,i,value);fm1_encoders_sample(s,rows);fm1_encoders_sample(s,rows);
}
static void test_capture_preserves_all_encoders_while_ui_is_paused(void) {
    fm1_encoders s={0};uint8_t rows[11];uint32_t sequence=0,last_sequence=0;
    int32_t previous[7]={0},remainder[7]={0};int steps[7]={0};unsigned i,j,k;
    const unsigned forward[]={2,3,1,0},reverse[]={1,3,2,0};
    memset(rows,0x3f,sizeof(rows));
    fm1_encoders_capture(&s,&last_sequence,++sequence,rows);
    for(j=0;j<20;j++)for(k=0;k<4;k++) {
        for(i=0;i<7;i++)contact(rows,i,forward[k]);
        fm1_encoders_capture(&s,&last_sequence,++sequence,rows);
        fm1_encoders_capture(&s,&last_sequence,++sequence,rows);
    }
    /* Completed scans continue while the UI makes no detent polls. */
    for(i=0;i<7;i++)CHECK(s.count[i]==80 && !s.invalid[i]);
    for(j=0;j<10;j++)for(i=0;i<7;i++)
        steps[i]+=fm1_encoder_detents(s.count[i],previous+i,remainder+i);
    for(i=0;i<7;i++)CHECK(steps[i]==20 && previous[i]==80 && !remainder[i]);
    for(j=0;j<20;j++)for(k=0;k<4;k++) {
        for(i=0;i<7;i++)contact(rows,i,reverse[k]);
        fm1_encoders_capture(&s,&last_sequence,++sequence,rows);
        fm1_encoders_capture(&s,&last_sequence,++sequence,rows);
    }
    for(i=0;i<7;i++)CHECK(!s.count[i] && !s.invalid[i]);
    for(j=0;j<10;j++)for(i=0;i<7;i++)
        steps[i]+=fm1_encoder_detents(s.count[i],previous+i,remainder+i);
    for(i=0;i<7;i++)CHECK(!steps[i] && !previous[i] && !remainder[i]);
}
static void test_duplicate_capture_does_not_qualify_or_repeat_an_edge(void) {
    fm1_encoders s={0};uint8_t rows[11];uint32_t last_sequence=0;
    memset(rows,0x3f,sizeof(rows));
    fm1_encoders_capture(&s,&last_sequence,0,rows);
    CHECK(!s.valid && !last_sequence);
    fm1_encoders_capture(&s,&last_sequence,1,rows);
    contact(rows,0,2);fm1_encoders_capture(&s,&last_sequence,2,rows);
    CHECK(s.previous[0]==0 && s.candidate[0]==2 && !s.count[0]);
    fm1_encoders_capture(&s,&last_sequence,2,rows);
    CHECK(s.previous[0]==0 && !s.count[0]);
    contact(rows,0,3);fm1_encoders_capture(&s,&last_sequence,2,rows);
    CHECK(s.candidate[0]==2 && !s.count[0]);
    contact(rows,0,2);fm1_encoders_capture(&s,&last_sequence,3,rows);
    CHECK(s.previous[0]==2 && s.count[0]==1);
    contact(rows,0,3);fm1_encoders_capture(&s,&last_sequence,3,rows);
    CHECK(s.candidate[0]==2 && s.count[0]==1);
    fm1_encoders_capture(&s,&last_sequence,4,rows);
    fm1_encoders_capture(&s,&last_sequence,4,rows);
    CHECK(s.previous[0]==2 && s.count[0]==1);
    fm1_encoders_capture(&s,&last_sequence,5,rows);
    CHECK(s.previous[0]==3 && s.count[0]==2 && !s.invalid[0]);
}
static void test_capture_accepts_sequence_wrap(void) {
    fm1_encoders s={0};uint8_t rows[11];uint32_t last_sequence=0;
    memset(rows,0x3f,sizeof(rows));
    fm1_encoders_capture(&s,&last_sequence,UINT32_MAX-1u,rows);
    contact(rows,6,2);fm1_encoders_capture(&s,&last_sequence,UINT32_MAX,rows);
    fm1_encoders_capture(&s,&last_sequence,0,rows);
    CHECK(!last_sequence && s.count[6]==1 && !s.invalid[6]);
    contact(rows,6,3);
    fm1_encoders_capture(&s,&last_sequence,1,rows);
    CHECK(last_sequence==1 && s.count[6]==1 && !s.invalid[6]);
    fm1_encoders_capture(&s,&last_sequence,2,rows);
    CHECK(last_sequence==2 && s.count[6]==2 && !s.invalid[6]);
}
static void test_detent_backlog_is_delivered_without_clipping(void) {
    int32_t previous=0,remainder=0;int total=0;unsigned i;
    for(i=0;i<10;i++) {
        int step=fm1_encoder_detents(80,&previous,&remainder);
        CHECK(step==2 && previous==(int32_t)(8*(i+1)) && !remainder);total+=step;
    }
    CHECK(total==20 && fm1_encoder_detents(80,&previous,&remainder)==0);
    total=0;
    for(i=0;i<10;i++) {
        int step=fm1_encoder_detents(0,&previous,&remainder);
        CHECK(step==-2 && previous==80-(int32_t)(8*(i+1)) && !remainder);total+=step;
    }
    CHECK(total==-20 && fm1_encoder_detents(0,&previous,&remainder)==0);
}
static void test_partial_detent_cancels_on_direction_reversal(void) {
    int32_t previous=0,remainder=0;
    CHECK(fm1_encoder_detents(3,&previous,&remainder)==0);
    CHECK(previous==3 && remainder==3);
    CHECK(fm1_encoder_detents(1,&previous,&remainder)==0);
    CHECK(previous==1 && remainder==1);
    CHECK(fm1_encoder_detents(0,&previous,&remainder)==0);
    CHECK(!previous && !remainder);
    CHECK(fm1_encoder_detents(-3,&previous,&remainder)==0);
    CHECK(previous==-3 && remainder==-3);
    CHECK(fm1_encoder_detents(-4,&previous,&remainder)==-1);
    CHECK(previous==-4 && !remainder);
}
static void test_detents_handle_signed_count_extremes_without_overflow(void) {
    int32_t previous=INT32_MIN,remainder=0;
    CHECK(fm1_encoder_detents(INT32_MAX,&previous,&remainder)==2);
    CHECK(previous==INT32_MIN+8 && !remainder);
    CHECK(fm1_encoder_detents(INT32_MAX,&previous,&remainder)==2);
    CHECK(previous==INT32_MIN+16 && !remainder);
    previous=INT32_MAX;
    CHECK(fm1_encoder_detents(INT32_MIN,&previous,&remainder)==-2);
    CHECK(previous==INT32_MAX-8 && !remainder);
    CHECK(fm1_encoder_detents(INT32_MIN,&previous,&remainder)==-2);
    CHECK(previous==INT32_MAX-16 && !remainder);
    previous=INT32_MAX-1;remainder=3;
    CHECK(fm1_encoder_detents(INT32_MAX,&previous,&remainder)==1);
    CHECK(previous==INT32_MAX && !remainder);
    previous=INT32_MIN+1;remainder=-3;
    CHECK(fm1_encoder_detents(INT32_MIN,&previous,&remainder)==-1);
    CHECK(previous==INT32_MIN && !remainder);
}
int main(void) {
    fm1_encoders s={0};uint8_t rows[11];unsigned i,j;int32_t peak=0;int64_t sum=0;
    memset(rows,0x3f,sizeof(rows));fm1_encoders_sample(&s,rows);
    for(i=0;i<7;i++) {
        const unsigned forward[]={2,3,1,0},reverse[]={1,3,2,0};
        for(j=0;j<4;j++)stable(&s,rows,i,forward[j]);
        CHECK(s.count[i]==4 && !s.invalid[i]);
        for(j=0;j<4;j++)stable(&s,rows,i,reverse[j]);
        CHECK(!s.count[i]);
        stable(&s,rows,i,3);CHECK(s.invalid[i]==1 && !s.count[i]);
        fm1_encoders_sample(&s,rows);CHECK(s.invalid[i]==1);
        stable(&s,rows,i,0);
    }
    CHECK(!strcmp(fm1_encoder_names[0],"SELECT") && !strcmp(fm1_encoder_names[6],"PRESETS"));
    CHECK(!strcmp(fm1_encoder_names[1],"ALGORITHM") && !strcmp(fm1_encoder_names[5],"KNOB4"));
    /* One-scan glitches never advance the accepted state or hide a stable
       illegal two-bit transition. Qualify all four starting phases. */
    for(i=0;i<7;i++)for(j=0;j<4;j++) {
        unsigned k;fm1_encoders t={0};
        memset(rows,0x3f,sizeof(rows));contact(rows,i,j);fm1_encoders_sample(&t,rows);
        for(k=0;k<4;k++) {
            contact(rows,i,k);fm1_encoders_sample(&t,rows);
            contact(rows,i,j);fm1_encoders_sample(&t,rows);fm1_encoders_sample(&t,rows);
            CHECK(t.previous[i]==j && !t.count[i] && !t.invalid[i]);
        }
        stable(&t,rows,i,j^3);CHECK(t.invalid[i]==1 && t.count[i]==0);
    }
    /* Twenty detents each way, every AB phase held for two scans; equal travel
       returns to zero. A 10 ms sampler of 2 ms phases instead misses states. */
    {
        fm1_encoders fast={0},slow={0};unsigned tick;
        const unsigned phases[]={0,2,3,1};
        memset(rows,0x3f,sizeof(rows));fm1_encoders_sample(&fast,rows);fm1_encoders_sample(&slow,rows);
        for(tick=1;tick<=640;tick++) {
            contact(rows,0,phases[(tick/8)%4]);
            fm1_encoders_sample(&fast,rows);
            if(tick%40==0)fm1_encoders_sample(&slow,rows);
        }
        fm1_encoders_sample(&fast,rows);
        CHECK(fast.count[0]==80 && fast.invalid[0]==0 && slow.count[0]!=80);
        for(tick=1;tick<=640;tick++) {
            contact(rows,0,phases[(4-(tick/8)%4)%4]);fm1_encoders_sample(&fast,rows);
        }
        fm1_encoders_sample(&fast,rows);CHECK(fast.count[0]==0 && !fast.invalid[0]);
        fast.count[0]=2147483647;stable(&fast,rows,0,2);CHECK(fast.count[0]==2147483647);
        fast.count[0]=(-2147483647-1);stable(&fast,rows,0,0);CHECK(fast.count[0]==(-2147483647-1));
    }
    test_capture_preserves_all_encoders_while_ui_is_paused();
    test_duplicate_capture_does_not_qualify_or_repeat_an_edge();
    test_capture_accepts_sequence_wrap();
    test_detent_backlog_is_delivered_without_clipping();
    test_partial_detent_cancels_on_direction_reversal();
    test_detents_handle_signed_count_extremes_without_overflow();
    for(i=0;i<FM1_TONE_FRAMES+1000;i++) {
        int32_t sample=fm1_test_sample(i);
        CHECK(sample>=-65536 && sample<=65536);
        if(i<44100 || i>=FM1_TONE_FRAMES)CHECK(sample==0);
        if(abs(sample)>peak)peak=abs(sample);
        sum+=sample;
    }
    CHECK(peak==65536 && fm1_test_sample(44100)==0 && fm1_test_sample(FM1_TONE_FRAMES-1)==0);
    CHECK(fm1_test_sample(0xffffffffu)==0);
    /* Envelope boundaries need not contain an integral number of periods.
     * Mean DC stays below two low-24-bit sample units over the tone. */
    CHECK(sum>-(int64_t)88200*2 && sum<(int64_t)88200*2);
    puts("PASS seven encoder mappings, stock stable-two filter, glitches, illegal transitions, fast/slow sampling, count saturation, snapshot capture, lossless detents, signed extremes, bounded tone");return 0;
}
