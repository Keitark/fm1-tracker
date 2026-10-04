#include "fm1_wl82.h"
#include "asm/iis.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static volatile uint32_t registers[9];
static unsigned accesses,prepared,finished,closed,muted,opened,irq_bound,stops,lock_depth;
static uint32_t now;
static int fail_prepare,fail_spi,fail_open,fail_rate,fail_irq,fail_unmute;
static int frozen_clock;
static void (*irq_handler)(void);
static void (*pcm_handler)(void *,u8 *,int,u8);
static struct iis_platform_data *active_pd;
static uint8_t *rom;
static size_t rom_size;
volatile uint32_t *fm1_test_register(uint32_t address){
    static const uint32_t addresses[]={0x50080,0x50088,0x5008c,0x50090,0x50094,0x51020,0x11d00,0x11d04,0x11d08};
    unsigned i;++accesses;
    for(i=0;i<9;++i)if(address==addresses[i]){
        if(address==0x11d00 && !fail_spi)registers[i]|=0x8000;
        return &registers[i];
    }
    CHECK(0);return 0;
}
static int prepare(void *c){(void)c;++prepared;muted=1;return fail_prepare;}
static int mute(void *c,int state){(void)c;if(!state && fail_unmute)return -1;muted=(unsigned)state;return 0;}
static void finish(void *c){(void)c;CHECK(!opened && !irq_bound && muted);++finished;}
static uint32_t clock_us(void *c){(void)c;if(!frozen_clock)now+=10;return now;}
static int wait_us(void *c,uint32_t n){(void)c;now+=n;if(irq_handler)irq_handler();return 0;}
static uint64_t keys(void *c){(void)c;return 0;}
static int stop(void *c){(void)c;return ++stops>=3;}
static int bind_irq(void *c,void (*h)(void)){(void)c;irq_bound=1;irq_handler=h;return fail_irq;}
static void unbind_irq(void *c){(void)c;irq_bound=0;irq_handler=0;}
static unsigned irq_save(void *c){(void)c;CHECK(!lock_depth);return lock_depth++;}
static void irq_restore(void *c,unsigned state){(void)c;CHECK(lock_depth==1 && state==0);lock_depth=state;}
int iis_open(struct iis_platform_data *p,u8 index){CHECK(index==0 && !opened);
    CHECK(p->port_sel==IIS_PORTC && p->data_width==8 && p->channel_in==0 && p->sr_points==128);
    CHECK(p->channel_out==8 && p->mclk_output==1 && p->slave_mode==0 && p->update_edge==0 && p->f32e==0);
    CHECK(registers[5]==0xa5000010u); /* Correct SPI mux and preserved unrelated bits. */
    if(fail_open)return -1;opened=1;active_pd=p;return 0;}
void iis_close(u8 index){CHECK(index==0 && opened && !irq_bound);opened=0;++closed;}
int iis_set_sample_rate(int rate,u8 index){CHECK(rate==44100 && index==0 && opened);return fail_rate;}
void iis_set_dec_data_handler(void *c,void (*cb)(void *,u8 *,int,u8),u8 index){CHECK(c==0 && index==0);pcm_handler=cb;}
void iis_channel_on(u8 channel,u8 index){CHECK(index==0 && channel==active_pd->channel_out && irq_bound && pcm_handler);}
void iis_channel_off(u8 channel,u8 index){CHECK(index==0 && channel==active_pd->channel_out && !irq_bound);}
void iis_irq_handler(u8 index){
    u8 buffer[514];int i;CHECK(index==0 && opened && irq_bound);
    memset(buffer,0xa5,sizeof(buffer));pcm_handler(0,buffer+1,512,3);
    CHECK(buffer[0]==0xa5 && buffer[513]==0xa5); /* unaligned destination, exact half */
    for(i=1;i<=512;++i)CHECK(buffer[i]==0); /* short run remains in startup hold */
    memset(buffer,0xa5,sizeof(buffer));pcm_handler(0,buffer+1,511,3);
    CHECK(buffer[0]==0xa5 && buffer[512]==0xa5 && buffer[513]==0xa5);
    for(i=1;i<=511;++i)CHECK(buffer[i]==0); /* reject fractional stereo frame */
    memset(buffer,0xa5,sizeof(buffer));pcm_handler(0,buffer+1,504,3);
    for(i=1;i<=504;++i)CHECK(buffer[i]==0); /* reject changed whole-frame geometry */
    CHECK(buffer[0]==0xa5 && buffer[505]==0xa5 && buffer[513]==0xa5);
    memset(buffer,0xa5,sizeof(buffer));pcm_handler(0,buffer+1,512,0);
    for(i=1;i<=512;++i)CHECK(buffer[i]==0); /* inactive channel */
    CHECK(buffer[0]==0xa5 && buffer[513]==0xa5);
}
static fm1_wl82_services services={0,prepare,mute,finish,clock_us,wait_us,keys,stop,bind_irq,unbind_irq,irq_save,irq_restore};
static fm1_board_config config;
static fm1_wl82_audio_config audio_config;
static void setup(void){
    memset((void *)registers,0,sizeof(registers));accesses=prepared=finished=closed=muted=opened=irq_bound=stops=lock_depth=0;
    now=0;fail_prepare=fail_spi=fail_open=fail_rate=fail_irq=fail_unmute=0;irq_handler=0;pcm_handler=0;
    frozen_clock=0;
    registers[5]=0xa5000000u;
    fm1_wl82_stock_audio_route(0);fm1_wl82_stock_audio_route(&audio_config);
    CHECK(audio_config.output_channel==3 && audio_config.mclk_output==1 &&
          audio_config.update_edge==0 && audio_config.sclk_32_per_frame==0 && accesses==0);
    fm1_board_default_config(&config);CHECK(fm1_wl82_install(&services,&config,&audio_config)==0);CHECK(accesses==0);
}
int main(int argc,char **argv){FILE *f;long size;
    CHECK(fm1_wl82_run(0,0)==FM1_NES_BSP_UNVERIFIED && accesses==0);
    CHECK(argc==2);f=fopen(argv[1],"rb");CHECK(f);CHECK(fseek(f,0,SEEK_END)==0);size=ftell(f);CHECK(size>0);rewind(f);
    rom_size=(size_t)size;rom=malloc(rom_size);CHECK(rom);CHECK(fread(rom,1,rom_size,f)==rom_size);fclose(f);
    setup();CHECK(fm1_wl82_run(rom,1)==FM1_NES_INVALID && prepared==0 && accesses==0);
    setup();fail_prepare=1;CHECK(fm1_wl82_run(rom,rom_size)==FM1_NES_BSP_UNVERIFIED && accesses==0 && finished==1);
    setup();fail_spi=1;CHECK(fm1_wl82_run(rom,rom_size)==FM1_NES_IO_ERROR && finished==1 && closed==0);
    setup();fail_spi=frozen_clock=1;
    CHECK(fm1_wl82_run(rom,rom_size)==FM1_NES_IO_ERROR && finished==1 && closed==0);
    setup();fail_open=1;CHECK(fm1_wl82_run(rom,rom_size)==FM1_NES_IO_ERROR && finished==1 && closed==0);
    setup();fail_rate=1;CHECK(fm1_wl82_run(rom,rom_size)==FM1_NES_IO_ERROR && finished==1 && closed==1);
    setup();fail_irq=1;CHECK(fm1_wl82_run(rom,rom_size)==FM1_NES_IO_ERROR && finished==1 && closed==1);
    setup();config.gain=16;CHECK(fm1_wl82_install(&services,&config,&audio_config)==0);fail_unmute=1;
    CHECK(fm1_wl82_run(rom,rom_size)==FM1_NES_IO_ERROR && finished==1 && closed==1);
    setup();{
        fm1_nes_stats stats;
        CHECK(fm1_wl82_run_with_stats(rom,rom_size,&stats)==0 && finished==1 && closed==1 && muted && stops==3);
        CHECK(stats.frames==3 && stats.audio_samples==3*735);
    }
    CHECK(fm1_wl82_run(rom,rom_size)==0 && finished==2 && closed==2); /* restart */
    setup();CHECK(fm1_wl82_install(0,&config,&audio_config)==FM1_NES_INVALID);
    CHECK(fm1_wl82_run(rom,rom_size)==FM1_NES_BSP_UNVERIFIED && accesses==0);
    setup();audio_config.sclk_32_per_frame=1;
    CHECK(fm1_wl82_install(&services,&config,&audio_config)==FM1_NES_INVALID);
    CHECK(fm1_wl82_run(rom,rom_size)==FM1_NES_BSP_UNVERIFIED && accesses==0);
    setup();{
        fm1_wl82_services digital=services;
        digital.mute=0;
        CHECK(fm1_wl82_install(&digital,&config,&audio_config)==FM1_NES_INVALID);
        digital.mute_policy=FM1_AUDIO_MUTE_DIGITAL_ONLY;
        config.gain=4;
        CHECK(fm1_wl82_install(&digital,&config,&audio_config)==0);
        CHECK(fm1_wl82_run(rom,rom_size)==0 && finished==1 && closed==1);
        digital.mute=mute;
        CHECK(fm1_wl82_install(&digital,&config,&audio_config)==FM1_NES_INVALID);
        digital.mute_policy=2;
        CHECK(fm1_wl82_install(&digital,&config,&audio_config)==FM1_NES_INVALID);
    }
    free(rom);puts("PASS: WL82 install gate, ROM gate, prepare failure, SPI timeout, IIS/IRQ/mute failures, cleanup, restart (FAKE hardware)");return 0;
}
