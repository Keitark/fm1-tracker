#ifndef FM1_FORGE_BENCHMARK_H
#define FM1_FORGE_BENCHMARK_H
#include <stdint.h>
#define FM1_FORGE_BENCH_TOTAL UINT32_C(16777216)
#define FM1_FORGE_BENCH_SLICE UINT32_C(65536)
#define FM1_FORGE_BENCH_TICK_US 500u
#define FM1_FORGE_BENCH_SLICE_TICKS 16u
#define FM1_FORGE_BENCH_EXPECTED_X UINT32_C(0xdb418d78)
#define FM1_FORGE_BENCH_EXPECTED_ACC UINT32_C(0xd1593f35)
#define FM1_FORGE_BENCH_EXPECTED_CHECKSUM UINT32_C(0x219bcd44)
typedef enum {
    FM1_FORGE_BENCH_IDLE, FM1_FORGE_BENCH_RUNNING, FM1_FORGE_BENCH_COMPLETE,
    FM1_FORGE_BENCH_CANCELLED, FM1_FORGE_BENCH_FAULT
} fm1_forge_bench_state;
typedef struct {
    fm1_forge_bench_state state;
    uint32_t done,x,acc,checksum,started,active_ticks,wall_ticks,batches;
    int fault;
} fm1_forge_benchmark;
typedef uint32_t (*fm1_forge_bench_clock)(void *);
void fm1_forge_bench_init(fm1_forge_benchmark *);
int fm1_forge_bench_start(fm1_forge_benchmark *,uint32_t);
void fm1_forge_bench_cancel(fm1_forge_benchmark *,uint32_t);
void fm1_forge_bench_abort(fm1_forge_benchmark *,uint32_t,int);
void fm1_forge_bench_step(fm1_forge_benchmark *,fm1_forge_bench_clock,void *);
const char *fm1_forge_bench_state_name(fm1_forge_bench_state);
#endif
