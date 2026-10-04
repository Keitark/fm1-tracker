#include "board_power.h"
#include "boot_trace.h"
#include "asm/power_interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
void board_init(void);
void board_early_init(void);
static unsigned calls,early,complete;
void fm1_boot_trace_mark(uint32_t stage) {
    if(stage==FM1_TRACE_EARLY_BOARD){CHECK(!calls && !fm1_power_stage);++early;}
    else {CHECK(stage==FM1_TRACE_BOARD && calls==1 && fm1_power_stage==2);++complete;}
}
void power_init(const struct low_power_param *p) {
    static const unsigned char expected[20]={0,0,0,0,0,0,0,0,0,0,0,0,4,3,7,1,15,0,7,1};
    CHECK(early==1 && !complete && !calls && fm1_power_stage==1);
    CHECK(sizeof(*p)==sizeof(expected) && !memcmp(p,expected,sizeof(expected)));
    ++calls;
}
int main(void) {
    CHECK(!fm1_power_stage);board_early_init();board_init();
    CHECK(calls==1 && complete==1 && fm1_power_stage==2);
    board_init();CHECK(calls==1 && complete==2); /* no repeated rail transition */
    puts("stock power parameters and board-stage ordering passed (mock SDK call)");return 0;
}
