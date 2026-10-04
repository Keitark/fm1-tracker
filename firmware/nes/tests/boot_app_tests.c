/* Execute the real headless app_main against a deterministic fake clock/core.
   This does not execute SDK startup or establish a hardware-integrity pass. */
#include "fm1_rom50.h"
#include "boot_trace.h"
void __real_memory_init(void) {}
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void app_main(void);
extern volatile uint32_t fm1_boot_stage,fm1_boot_elapsed_ms,fm1_boot_heartbeat;
extern volatile int fm1_boot_result;
extern volatile int fm1_boot_task_error;
extern volatile fm1_nes_stats fm1_boot_stats;
static jmp_buf done;
static uint32_t now,frame_cost,waits;
static int core_result,frozen;
static int create_result;
static void (*scheduled_task)(void *);
#define CHECK(c) do {if(!(c)) {fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}} while(0)

uint32_t timer_get_ms(void) {return now;}
int task_create(void (*task)(void *),void *arg,const char *name) {
    CHECK(task!=NULL && arg==NULL && strcmp(name,"fm1_nes")==0);
    CHECK(fm1_boot_stage==1);
    CHECK(fm1_boot_trace.last==FM1_TRACE_APP);
    if(!create_result)scheduled_task=task;
    return create_result;
}
void os_time_dly(int ticks) {
    ++waits;
    if(frozen && waits==3000) {
        CHECK(fm1_boot_stage==4);
        CHECK(fm1_boot_elapsed_ms==0);
        longjmp(done,1);
    }
    if(ticks==100) {
        CHECK(fm1_boot_stage==(core_result?255u:3u));
        if(!core_result)CHECK(fm1_boot_elapsed_ms>=20000u);
        CHECK(fm1_boot_heartbeat>0);
        longjmp(done,1);
    }
    CHECK(ticks==1);
    CHECK(fm1_boot_stage==2 || fm1_boot_stage==4);
    if(!frozen)now+=10u;
}
const uint8_t *fm1_rom50_data(size_t *size) {
    static const uint8_t rom[16]={0};*size=sizeof(rom);return rom;
}
int fm1_nes_run(const uint8_t *rom,size_t size,const fm1_nes_platform *p,
                uint32_t frames,fm1_nes_stats *stats) {
    uint32_t i;
    CHECK(rom!=NULL && size==16 && frames==600);
    CHECK(fm1_boot_stage==2);
    for(i=0;i<frames;++i) {
        if(!frozen)now+=frame_cost;
        CHECK(p->frame(p->context,i)==0);
    }
    stats->frames=frames;
    return core_result;
}
static void run(uint32_t start,uint32_t extra,int error,int stopped_clock) {
    now=start;frame_cost=extra;core_result=error;frozen=stopped_clock;waits=0;
    create_result=0;scheduled_task=NULL;
    app_main(); /* Must return without running NES or waiting for any timer. */
    CHECK(waits==0 && scheduled_task!=NULL && fm1_boot_stage==1);
    CHECK(fm1_boot_task_error==0);
    if(setjmp(done)==0)scheduled_task(NULL);
    CHECK(fm1_boot_result==error);
    CHECK(fm1_boot_stats.frames==600);
    if(!error && !stopped_clock) {
        uint32_t expected=600u*(10u+extra);
        if(expected<20000u)expected=20000u;
        CHECK(fm1_boot_elapsed_ms==expected);
    }
}
int main(void) {
    run(0,0,0,0);                   /* Short run waits until 20 seconds. */
    run(100,40,0,0);                /* Long run doesn't add 20 more seconds. */
    run(UINT32_MAX-4000u,0,0,0);     /* Unsigned clock rollover. */
    run(0,0,FM1_NES_IO_ERROR,0);     /* Emulator error never becomes success. */
    run(0,0,0,1);                   /* A frozen clock cannot complete soak. */
    create_result=7;scheduled_task=NULL;waits=0;
    app_main();
    CHECK(scheduled_task==NULL && waits==0);
    CHECK(fm1_boot_stage==255 && fm1_boot_task_error==7);
    CHECK(fm1_boot_result==FM1_NES_IO_ERROR);
    puts("headless observation-window tests passed");
    return 0;
}
