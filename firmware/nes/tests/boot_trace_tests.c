#include "boot_trace.h"
#include "clock_contract.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned called;
void __real_memory_init(void) {
    CHECK(fm1_boot_trace.magic==0x464d4254u && fm1_boot_trace.version==1);
    CHECK(fm1_boot_trace.last==FM1_TRACE_CACHE_READY);
    CHECK(fm1_boot_trace.visited==(1u<<FM1_TRACE_CACHE_READY));
    ++called;
}
int main(void) {
    unsigned i;
    CHECK(!fm1_boot_trace.magic);
    __wrap_memory_init();
    CHECK(called==1 && fm1_boot_trace.last==FM1_TRACE_MEMORY_READY);
    for(i=FM1_TRACE_EARLY_BOARD;i<=FM1_TRACE_WORKER;++i)fm1_boot_trace_mark(i);
    CHECK(fm1_boot_trace.visited==0x7e && fm1_boot_trace.last==FM1_TRACE_WORKER);
    fm1_boot_trace_mark(0);fm1_boot_trace_mark(32);fm1_boot_trace_mark(UINT32_MAX);
    CHECK(fm1_boot_trace.visited==0x7e && fm1_boot_trace.last==FM1_TRACE_WORKER);
    __wrap_memory_init();CHECK(called==2 && fm1_boot_trace.visited==6);
    CHECK(fm1_clock_report_valid(24000000,1));
    CHECK(fm1_clock_report_valid(396000000,60000000));
    CHECK(fm1_clock_report_valid(480000000,80000000));
    CHECK(!fm1_clock_report_valid(23999999,60000000));
    CHECK(!fm1_clock_report_valid(480000001,60000000));
    CHECK(!fm1_clock_report_valid(-1,60000000));
    CHECK(!fm1_clock_report_valid(480000000,0));
    CHECK(!fm1_clock_report_valid(480000000,80000001));
    puts("boot trace order and reported-clock contract passed (host only)");return 0;
}
