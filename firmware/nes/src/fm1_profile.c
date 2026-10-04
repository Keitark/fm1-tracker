#include "fm1_profile.h"
#include <string.h>
static fm1_profile_stats stats;
static uint32_t (*clock_now)(void);
static uint32_t previous, origin;
static unsigned current;
static void account(void) {
    uint32_t now=clock_now();
    stats.ticks[current]+=(uint32_t)(now-previous);
    stats.wall=(uint32_t)(now-origin);previous=now;
}
void fm1_profile_start(uint32_t (*clock)(void)) {
    memset(&stats,0,sizeof(stats));clock_now=clock;current=FM1_PROF_OTHER;
    previous=origin=clock?clock():0;
}
unsigned fm1_profile_enter(unsigned section) {
    unsigned old=current;
    if(!clock_now || section>=FM1_PROF_COUNT)return old;
    account();++stats.transitions;current=section;return old;
}
void fm1_profile_snapshot(fm1_profile_stats *out) {
    if(clock_now)account();
    if(out)*out=stats;
}
