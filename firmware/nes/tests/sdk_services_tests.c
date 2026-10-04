#include "wl82_services.h"
#include "fm1_wl82_keyscan.h"
#include "fake_sdk_services.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static __declspec(thread) unsigned irq_flags=0xabcd0100u,held;
static fm1_wl82_services service;
static uint32_t milliseconds;
static unsigned watchdogs,delays,starts,scans,stops;
static int clock_bad,timer_stuck,scan_fail,start_fail;
static int reported_sys=240000000;
static LONG handled,unregistered;
static void (*registered_isr)(void);
static HANDLE handler_entered,handler_release,irq_masked;
static int concurrent;
unsigned fm1_fake_irq_save(void){unsigned f=irq_flags;irq_flags&=~0x100u;return f;}
void fm1_fake_irq_restore(unsigned f){irq_flags=f;}
void fm1_fake_lock(spinlock_t *l){CHECK(!(irq_flags&0x100));AcquireSRWLockExclusive(&l->native);++held;}
void fm1_fake_unlock(spinlock_t *l){CHECK(held && !(irq_flags&0x100));--held;ReleaseSRWLockExclusive(&l->native);}
void fm1_fake_barrier(void){MemoryBarrier();}
uint32_t timer_get_ms(void){return milliseconds;}
void os_time_dly(int ticks){CHECK(ticks==1 && !held && (irq_flags&0x100));++delays;if(!timer_stuck)milliseconds+=10;}
void wdt_clear(void){++watchdogs;}
int clk_get(const char *n){return !strcmp(n,"sys")?(clock_bad?0:reported_sys):60000000;}
void request_irq(unsigned char n,unsigned char p,void (*h)(void),unsigned char cpu){
    CHECK(n==11 && p==3 && cpu==0 && h && !registered_isr && !held);registered_isr=h;
}
void bit_clr_ie(unsigned char n,unsigned char cpu){CHECK(n==11 && cpu==0);if(concurrent)SetEvent(irq_masked);}
void unrequest_irq(unsigned char n,unsigned char cpu){CHECK(n==11 && cpu==0 && !held);registered_isr=0;InterlockedIncrement(&unregistered);}
int fm1_wl82_keyscan_start(fm1_wl82_keyscan *s,void *ctx,uint32_t (*clock)(void *)){
    ++starts;CHECK(!s->running && clock(ctx)==milliseconds*1000u);
    if(start_fail)return FM1_NES_IO_ERROR;
    s->running=1;return 0;
}
int fm1_wl82_keyscan_poll(fm1_wl82_keyscan *s,uint64_t *keys){
    CHECK(s->running);++scans;if(scan_fail)return FM1_NES_IO_ERROR;
    *keys=(UINT64_C(1)<<40)|(UINT64_C(1)<<18);return 0;
}
void fm1_wl82_keyscan_stop(fm1_wl82_keyscan *s){if(s->running){++stops;s->running=0;}}
static void audio_handler(void){
    unsigned flags;CHECK(held==1 && !(irq_flags&0x100));
    flags=service.irq_save(0);CHECK(held==2);service.irq_restore(0,flags);
    CHECK(held==1 && !(irq_flags&0x100));
    if(concurrent){SetEvent(handler_entered);CHECK(WaitForSingleObject(handler_release,3000)==WAIT_OBJECT_0);}
    InterlockedIncrement(&handled);
}
static DWORD WINAPI run_irq(void *p){void (*h)(void)=registered_isr;(void)p;CHECK(h);h();CHECK(!held && irq_flags==0xabcd0100u);return 0;}
static DWORD WINAPI stop_irq(void *p){(void)p;service.audio_irq_stop(0);CHECK(!held && irq_flags==0xabcd0100u);return 0;}
int main(void){
    unsigned flags;uint32_t before;HANDLE irq_thread,stop_thread;void (*stale)(void);
    CHECK(fm1_sdk_services_init(0)==FM1_NES_INVALID);
    CHECK(fm1_sdk_services_init(&service)==0 && !service.mute && service.mute_policy==FM1_AUDIO_MUTE_DIGITAL_ONLY);
    CHECK(!starts && !delays && !registered_isr); /* installation is no I/O */
    CHECK(service.audio_irq_start(0,audio_handler)==FM1_NES_INVALID);
    reported_sys=480000000;CHECK(service.prepare(0)==0 && fm1_sdk_diag.sys_hz==480000000);
    service.finish(0);starts=stops=0;reported_sys=240000000;
    clock_bad=1;CHECK(service.prepare(0)==FM1_NES_BSP_UNVERIFIED && !starts);service.finish(0);clock_bad=0;
    timer_stuck=1;CHECK(service.prepare(0)==FM1_NES_IO_ERROR && !starts);service.finish(0);timer_stuck=0;
    start_fail=1;CHECK(service.prepare(0)==FM1_NES_IO_ERROR);service.finish(0);start_fail=0;
    CHECK(service.prepare(0)==0 && fm1_sdk_diag.sys_hz==240000000 && fm1_sdk_diag.lsb_hz==60000000);
    CHECK(fm1_sdk_services_init(&service)==FM1_NES_BUSY);
    CHECK(service.read_keys(0)==((UINT64_C(1)<<40)|(UINT64_C(1)<<18)) && fm1_sdk_diag.scans==1);
    flags=service.irq_save(0);CHECK(flags==0xabcd0100u && held==1);
    service.irq_restore(0,flags);CHECK(irq_flags==0xabcd0100u && !held);
    before=milliseconds;CHECK(service.wait_us(0,0)==0 && milliseconds==before);
    CHECK(service.wait_us(0,1000)==0 && milliseconds-before>=2);
    CHECK(service.wait_us(0,UINT32_MAX)==FM1_NES_INVALID);
    milliseconds=UINT32_MAX-5;before=service.now_us(0);
    CHECK(service.wait_us(0,20000)==0 && (uint32_t)(service.now_us(0)-before)>=21000);
    CHECK(service.audio_irq_start(0,0)==FM1_NES_INVALID);
    CHECK(service.audio_irq_start(0,audio_handler)==0);
    CHECK(service.audio_irq_start(0,audio_handler)==FM1_NES_BUSY);
    registered_isr();CHECK(handled==1 && fm1_sdk_diag.audio_irqs==1 && !held && irq_flags==0xabcd0100u);
    /* Use two actual host threads to prove stop waits for an in-flight handler,
       including its nested queue lock. This models serialization, not MMIO. */
    handler_entered=CreateEvent(0,TRUE,FALSE,0);handler_release=CreateEvent(0,TRUE,FALSE,0);irq_masked=CreateEvent(0,TRUE,FALSE,0);
    CHECK(handler_entered && handler_release && irq_masked);concurrent=1;stale=registered_isr;
    irq_thread=CreateThread(0,0,run_irq,0,0,0);CHECK(irq_thread);
    CHECK(WaitForSingleObject(handler_entered,3000)==WAIT_OBJECT_0);
    stop_thread=CreateThread(0,0,stop_irq,0,0,0);CHECK(stop_thread);
    CHECK(WaitForSingleObject(irq_masked,3000)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(stop_thread,0)==WAIT_TIMEOUT && unregistered==0);
    SetEvent(handler_release);
    CHECK(WaitForSingleObject(irq_thread,3000)==WAIT_OBJECT_0 && WaitForSingleObject(stop_thread,3000)==WAIT_OBJECT_0);
    CHECK(handled==2 && unregistered==1 && !registered_isr);
    stale();CHECK(handled==2 && fm1_sdk_diag.audio_irqs==2); /* queued wrapper sees NULL */
    CloseHandle(irq_thread);CloseHandle(stop_thread);CloseHandle(handler_entered);CloseHandle(handler_release);CloseHandle(irq_masked);concurrent=0;
    service.audio_irq_stop(0);CHECK(unregistered==1);
    scan_fail=1;CHECK(service.read_keys(0)==0 && service.stop_requested(0));
    CHECK(service.wait_us(0,100)==FM1_NES_IO_ERROR);service.finish(0);CHECK(stops==1);
    scan_fail=0;CHECK(service.prepare(0)==0 && !service.stop_requested(0));
    CHECK(service.audio_irq_start(0,audio_handler)==0);service.finish(0);
    CHECK(stops==2 && unregistered==2 && !registered_isr && watchdogs>0);
    CHECK(service.prepare(0)==0);timer_stuck=1;before=delays;
    CHECK(service.wait_us(0,100000)==FM1_NES_IO_ERROR);
    CHECK(delays-before==1125 && fm1_sdk_diag.fault==FM1_NES_IO_ERROR);
    timer_stuck=0;service.finish(0);
    CHECK(fm1_sdk_services_init(&service)==0);
    puts("PASS: SDK service lifecycle, clock bounds/wrap, watchdog, scanner faults, IRQ flags, cross-thread teardown (host model)");return 0;
}
