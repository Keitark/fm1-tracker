#include "fm1_profile.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t tick;
static uint32_t clock_now(void){return tick;}
int main(void) {
    fm1_profile_stats s;unsigned outer,inner,i;uint32_t sum;
    fm1_profile_start(0);fm1_profile_enter(FM1_PROF_CPU);fm1_profile_snapshot(&s);CHECK(!s.wall);
    tick=100;fm1_profile_start(clock_now);
    tick=110;outer=fm1_profile_enter(FM1_PROF_APU);
    tick=120;inner=fm1_profile_enter(FM1_PROF_FX);
    tick=150;fm1_profile_enter(inner);
    tick=170;fm1_profile_enter(outer);
    tick=175;fm1_profile_snapshot(&s);
    CHECK(s.ticks[FM1_PROF_OTHER]==15 && s.ticks[FM1_PROF_APU]==30 && s.ticks[FM1_PROF_FX]==30);
    CHECK(s.wall==75 && s.transitions==4);
    for(i=sum=0;i<FM1_PROF_COUNT;++i)sum+=s.ticks[i];CHECK(sum==s.wall);
    fm1_profile_enter(FM1_PROF_COUNT);fm1_profile_snapshot(&s);CHECK(s.transitions==4);
    tick=0xfffffff0u;fm1_profile_start(clock_now);fm1_profile_enter(FM1_PROF_CPU);
    tick=0x10;fm1_profile_snapshot(&s);CHECK(s.wall==32 && s.ticks[FM1_PROF_CPU]==32);
    puts("PASS profile: nesting, exclusive sum, disabled clock, reset, wrap, invalid scope");return 0;
}
