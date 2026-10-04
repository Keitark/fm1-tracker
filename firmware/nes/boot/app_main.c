#ifdef FM1_BOOT_APP_TEST
#include "fake_boot_app.h"
#else
#include "app_config.h"
#include "system/includes.h"
#include "system/task.h"
#include "os/os_api.h"
#include "system/sys_time.h"
#endif
#include "fm1_rom50.h"
#include "boot_trace.h"
#ifdef FM1_BOOT_DISPLAY_TEST
#include "display_test.h"
#endif
#ifdef FM1_BOOT_PERIPHERALS
#include "wl82_services.h"
#include "fm1_stock_keys.h"
#endif

const struct irq_info irq_info_table[] = {{-1,-1,-1}};
const struct task_info task_info_table[] = {
    {"app_core",15,4096,1024},
    {"sys_event",29,512,0},
    {"systimer",14,256,0},
    {"sys_timer",9,512,128},
    {"fm1_nes",10,4096,0},
    {0,0,0,0,0},
};

/* Inspect with a future debugger/RAM read, never mistaken for a hardware pass.
   1: app_main entered; 2: game running; 3: run completed; 4: headless soak;
   0xff: error. Stage 3 is NOT an SDK-integrity or hardware acceptance result.
   Headless remains the default; peripherals require an explicit build profile. */
volatile uint32_t fm1_boot_stage;
volatile int fm1_boot_result;
volatile fm1_nes_stats fm1_boot_stats;
volatile uint32_t fm1_boot_elapsed_ms;
volatile uint32_t fm1_boot_heartbeat;
volatile int fm1_boot_task_error;
static uint32_t boot_started_ms;
static void diagnostic_tick(void) {
    fm1_boot_elapsed_ms=(uint32_t)(timer_get_ms()-boot_started_ms);
    ++fm1_boot_heartbeat;
}
#if !defined(FM1_BOOT_PERIPHERALS) && !defined(FM1_BOOT_DISPLAY_TEST)
static int video(void *ctx,unsigned y,unsigned rows,const uint16_t *pixels) {
    (void)ctx;
    return !pixels || y+rows>240;
}
static int audio(void *ctx,const uint8_t *samples,size_t count) {
    (void)ctx;
    return !samples || !count;
}
static uint8_t buttons(void *ctx) {(void)ctx;return 0;}
static int frame(void *ctx,uint32_t count) {
    (void)ctx;(void)count;
    /* Yield to SDK watchdog/system tasks; this is NOT a 60 Hz scheduler. */
    os_time_dly(1);
    diagnostic_tick();
    return 0;
}
#endif
static void fm1_nes_task(void *arg) {
    fm1_boot_trace_mark(FM1_TRACE_WORKER);
#ifdef FM1_BOOT_DISPLAY_TEST
    (void)arg;
    boot_started_ms=timer_get_ms();
    fm1_boot_elapsed_ms=0;fm1_boot_heartbeat=0;
    fm1_boot_stats=(fm1_nes_stats){0};
    fm1_boot_stage=2;
#ifdef FM1_BOOT_EARLY_DISPLAY
    /* Board hook already produced the first frame, without scheduler waits.
     * Never reinitialize here: retain first-error evidence and panel state. */
    fm1_boot_result=fm1_display_error;
    if(!fm1_boot_result && fm1_display_stage!=4)fm1_boot_result=-4;
#else
    fm1_boot_result=fm1_display_test_init();
#endif
    while(!fm1_boot_result) {
        diagnostic_tick();
        fm1_boot_result=fm1_display_test_frame(fm1_boot_elapsed_ms);
        os_time_dly(10);
    }
    fm1_display_test_stop();fm1_boot_stage=0xff;
    for(;;){diagnostic_tick();os_time_dly(100);}
#else
    size_t size;
    const uint8_t *rom;
    fm1_nes_stats stats={0};
#ifdef FM1_BOOT_PERIPHERALS
    fm1_wl82_services services;
    fm1_board_config config;
    fm1_wl82_audio_config route;
#else
    fm1_nes_platform p={0,video,audio,buttons,frame};
#endif
    (void)arg;
    boot_started_ms=timer_get_ms();
    fm1_boot_elapsed_ms=0;
    fm1_boot_heartbeat=0;
    fm1_boot_stats=stats;
    rom=fm1_rom50_data(&size);
#ifdef FM1_BOOT_PERIPHERALS
    fm1_board_default_config(&config);
    config.gain=4; /* conservative digital level; NOT hardware amplifier mute */
    config.y_offset=40; /* recovered setup-table origin; bench qualification pending */
    fm1_wl82_stock_audio_route(&route);
    fm1_boot_result=fm1_stock_assign_note_keys(&config);
    if(!fm1_boot_result)fm1_boot_result=fm1_sdk_services_init(&services);
    if(!fm1_boot_result)fm1_boot_result=fm1_wl82_install(&services,&config,&route);
    if(!fm1_boot_result) {
        fm1_boot_stage=2;
        fm1_boot_result=fm1_wl82_run_with_stats(rom,size,&stats);
        if(fm1_sdk_diag.fault)fm1_boot_result=FM1_NES_IO_ERROR;
    }
#else
    fm1_boot_stage=2;
    fm1_boot_result=fm1_nes_run(rom,size,&p,600,&stats);
#endif
    fm1_boot_stats=stats;
#ifndef FM1_BOOT_PERIPHERALS
    if(!fm1_boot_result) {
        /* SDK schedules _mkey_check about 8 s after late init. Do not report
           a short diagnostic as completed before that observation window.
           We neither call/skip that check nor infer its result from uptime. */
        fm1_boot_stage=4;
        diagnostic_tick();
        while(fm1_boot_elapsed_ms<20000u) {
            os_time_dly(1);
            diagnostic_tick();
        }
    }
#endif
    fm1_boot_stage=fm1_boot_result?0xff:3;
    for(;;) {
        diagnostic_tick();
        os_time_dly(100);
    }
#endif
}

void app_main(void) {
    int rc;
    fm1_boot_trace_mark(FM1_TRACE_APP);
    fm1_boot_stage=1;
    fm1_boot_task_error=0;
    /* The SDK dispatches app_core events only AFTER app_main returns.
       Keeping the emulator here would starve queued timers, including the
       SDK's integrity callback. NES runs below app_core's priority instead. */
    rc=task_create(fm1_nes_task,NULL,"fm1_nes");
    if(rc) {
        fm1_boot_task_error=rc;
        fm1_boot_result=FM1_NES_IO_ERROR;
        fm1_boot_stage=0xff;
    }
}
