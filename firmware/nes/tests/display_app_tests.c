/* This link has no ROM, emulator, audio or scanner implementation. */
#include "fm1_nes.h"
#include "boot_trace.h"
void __real_memory_init(void) {}
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
void app_main(void);
extern volatile uint32_t fm1_boot_stage,fm1_boot_elapsed_ms;
extern volatile int fm1_boot_result,fm1_boot_task_error;
static jmp_buf done;
static void (*worker)(void *);
static int create_error,init_error,frames,stopped;
static uint32_t now;
uint32_t timer_get_ms(void){return now;}
int task_create(void (*f)(void *),void *arg,const char *name) {
    CHECK(!arg && !strcmp(name,"fm1_nes"));if(!create_error)worker=f;return create_error;
}
int fm1_display_test_init(void){CHECK(fm1_boot_trace.last==FM1_TRACE_WORKER);return init_error;}
int fm1_display_test_frame(uint32_t elapsed) {
    CHECK(elapsed==now);++frames;return frames==3?-2:0;
}
void fm1_display_test_stop(void){++stopped;}
void os_time_dly(int ticks) {
    if(ticks==100){CHECK(fm1_boot_stage==255);longjmp(done,1);}
    CHECK(ticks==10);now+=100;
}
static void run(int error) {
    worker=NULL;create_error=0;init_error=error;frames=stopped=0;now=0;
    app_main();CHECK(worker && fm1_boot_stage==1 && frames==0);
    if(!setjmp(done))worker(NULL);
    CHECK(stopped==1 && fm1_boot_result==(error?error:-2));
    CHECK(frames==(error?0:3));
}
int main(void) {
    run(0);run(-3);create_error=9;worker=NULL;
    app_main();CHECK(!worker && fm1_boot_task_error==9 && fm1_boot_stage==255);
    puts("display worker/dispatch/failure tests passed");return 0;
}
