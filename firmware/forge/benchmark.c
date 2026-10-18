/* Asset-free deterministic integer workload. Only the peripheral task calls
 * step: interrupts stay enabled and no device spinlock is held during work. */
#include "benchmark.h"
#include <string.h>
#if defined(_MSC_VER)
#define FM1_BENCH_NOINLINE __declspec(noinline)
#else
#define FM1_BENCH_NOINLINE __attribute__((noinline,used))
#endif
void fm1_forge_bench_init(fm1_forge_benchmark *b){memset(b,0,sizeof(*b));}
int fm1_forge_bench_start(fm1_forge_benchmark *b,uint32_t now){
    if(b->state==FM1_FORGE_BENCH_RUNNING)return -1;
    fm1_forge_bench_init(b);b->state=FM1_FORGE_BENCH_RUNNING;
    b->x=UINT32_C(0x12345678);b->started=now;return 0;
}
void fm1_forge_bench_cancel(fm1_forge_benchmark *b,uint32_t now){
    if(b->state==FM1_FORGE_BENCH_RUNNING){b->state=FM1_FORGE_BENCH_CANCELLED;b->wall_ticks=now-b->started;}
}
void fm1_forge_bench_abort(fm1_forge_benchmark *b,uint32_t now,int fault){
    if(b->state==FM1_FORGE_BENCH_RUNNING){b->state=FM1_FORGE_BENCH_FAULT;b->fault=fault;b->wall_ticks=now-b->started;}
}
FM1_BENCH_NOINLINE void fm1_forge_bench_step(fm1_forge_benchmark *b,fm1_forge_bench_clock clock,void *ctx){
    uint32_t start,end,n,x,acc,check;
    if(b->state!=FM1_FORGE_BENCH_RUNNING)return;
    start=clock(ctx);x=b->x;acc=b->acc;check=b->checksum;
    for(n=0;n<FM1_FORGE_BENCH_SLICE && b->done+n<FM1_FORGE_BENCH_TOTAL;){
        x^=x<<13;x^=x>>17;x^=x<<5;
        acc+=(x&UINT32_C(0xffff))*(x>>16);
        check=((check<<5)|(check>>27))^x^acc;n++;
        /* Timer reads every1024 iterations bound a slow-clock workload. The
         * boundary can exceed8ms by at most one1024-iteration chunk. */
        if(!(n&1023u) && (uint32_t)(clock(ctx)-start)>=FM1_FORGE_BENCH_SLICE_TICKS)break;
    }
    end=clock(ctx);b->x=x;b->acc=acc;b->checksum=check;b->done+=n;
    b->active_ticks+=end-start;b->wall_ticks=end-b->started;b->batches++;
    if(b->done==FM1_FORGE_BENCH_TOTAL){
        b->state=FM1_FORGE_BENCH_COMPLETE;
        if(x!=FM1_FORGE_BENCH_EXPECTED_X || acc!=FM1_FORGE_BENCH_EXPECTED_ACC || check!=FM1_FORGE_BENCH_EXPECTED_CHECKSUM){b->state=FM1_FORGE_BENCH_FAULT;b->fault=-84;}
    }
}
const char *fm1_forge_bench_state_name(fm1_forge_bench_state state){
    switch(state){case FM1_FORGE_BENCH_IDLE:return "idle";case FM1_FORGE_BENCH_RUNNING:return "running";
    case FM1_FORGE_BENCH_COMPLETE:return "complete";case FM1_FORGE_BENCH_CANCELLED:return "cancelled";
    case FM1_FORGE_BENCH_FAULT:return "fault";default:return "invalid";}
}
