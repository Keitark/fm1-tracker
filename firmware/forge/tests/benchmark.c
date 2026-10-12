#include "benchmark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(v) do{if(!(v)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#v);exit(1);}}while(0)
typedef struct {uint32_t tick,increment,calls;} fake_clock;
static uint32_t ticks(void *ctx){fake_clock *c=(fake_clock *)ctx;uint32_t value=c->tick;c->tick+=c->increment;c->calls++;return value;}
static void oracle_and_slice(void){
    fm1_forge_benchmark b;fake_clock c={0,0,0};uint32_t previous;
    fm1_forge_bench_init(&b);CHECK(b.state==FM1_FORGE_BENCH_IDLE);
    fm1_forge_bench_step(&b,ticks,&c);CHECK(!c.calls);
    CHECK(!fm1_forge_bench_start(&b,0));CHECK(fm1_forge_bench_start(&b,4)==-1);
    while(b.state==FM1_FORGE_BENCH_RUNNING){previous=b.done;fm1_forge_bench_step(&b,ticks,&c);CHECK(b.done-previous==FM1_FORGE_BENCH_SLICE);
        if(b.done==UINT32_C(1048576)){CHECK(b.x==UINT32_C(0xc99ffa61));CHECK(b.acc==UINT32_C(0xa74a62dd));CHECK(b.checksum==UINT32_C(0x62ce71ca));}}
    CHECK(b.state==FM1_FORGE_BENCH_COMPLETE && b.done==FM1_FORGE_BENCH_TOTAL && b.batches==256);
    CHECK(b.x==FM1_FORGE_BENCH_EXPECTED_X && b.acc==FM1_FORGE_BENCH_EXPECTED_ACC && b.checksum==FM1_FORGE_BENCH_EXPECTED_CHECKSUM);
    CHECK(!b.active_ticks && !b.wall_ticks && !b.fault);
    {fm1_forge_benchmark result=b;fm1_forge_bench_step(&b,ticks,&c);fm1_forge_bench_cancel(&b,100);CHECK(!memcmp(&result,&b,sizeof(b)));}
}
static void time_bound_and_wrap(void){
    fm1_forge_benchmark b;fake_clock c={UINT32_MAX-8u,1,0};
    fm1_forge_bench_init(&b);CHECK(!fm1_forge_bench_start(&b,c.tick));
    fm1_forge_bench_step(&b,ticks,&c);
    CHECK(b.done==16u*1024u && b.batches==1 && b.active_ticks==17 && b.wall_ticks==17);
    CHECK(b.state==FM1_FORGE_BENCH_RUNNING);fm1_forge_bench_step(&b,ticks,&c);
    CHECK(b.done==32u*1024u && b.batches==2 && b.active_ticks==34 && b.wall_ticks==35);
    fm1_forge_bench_cancel(&b,c.tick);CHECK(b.state==FM1_FORGE_BENCH_CANCELLED && b.wall_ticks==36);
    CHECK(!fm1_forge_bench_start(&b,50));CHECK(!b.done && !b.active_ticks && !b.wall_ticks && !b.batches && !b.fault && b.x==UINT32_C(0x12345678));
    fm1_forge_bench_abort(&b,73,-7);CHECK(b.state==FM1_FORGE_BENCH_FAULT && b.fault==-7 && b.wall_ticks==23);
    fm1_forge_bench_cancel(&b,90);CHECK(b.state==FM1_FORGE_BENCH_FAULT && b.fault==-7);
    CHECK(!fm1_forge_bench_start(&b,100));CHECK(!b.fault && b.state==FM1_FORGE_BENCH_RUNNING);
}
static void corruption_rejected(void){
    fm1_forge_benchmark b;fake_clock c={0,0,0};fm1_forge_bench_init(&b);CHECK(!fm1_forge_bench_start(&b,0));
    b.x^=1;while(b.state==FM1_FORGE_BENCH_RUNNING)fm1_forge_bench_step(&b,ticks,&c);
    CHECK(b.state==FM1_FORGE_BENCH_FAULT && b.fault==-84 && b.done==FM1_FORGE_BENCH_TOTAL);
}
int main(void){oracle_and_slice();time_bound_and_wrap();corruption_rejected();puts("Deterministic benchmark oracles, bounded slices, timer wrap, retained results, restart and fault cancellation passed");return 0;}
