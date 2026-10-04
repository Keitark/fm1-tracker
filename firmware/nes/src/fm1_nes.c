#include "fm1_nes.h"
#include "fm1_profile.h"
#include "nes.h"
#include <string.h>

static nes_t machine;
static fm1_nes_platform platform;
static fm1_nes_stats stats;
static uint32_t frame_limit;
static int active, io_error;
#ifdef FM1_NES_HOST_TEST
const void *fm1_nes_test_machine(void){return &machine;}
#endif
_Static_assert(sizeof(nes_t) <= 96u*1024u, "NES state exceeds initial RAM budget");
#ifdef FM1_TARGET_PI32V2
_Static_assert(sizeof(void *) == 4, "Expected 32-bit pi32v2 ABI");
#endif

static void nrom_ignore_write(nes_t *n,uint16_t address,uint8_t value) {
    (void)n;(void)address;(void)value; /* NROM PRG is read-only. */
}

static void cnrom_write(nes_t *n,uint16_t address,uint8_t value) {
    unsigned i;
    /* Standard 8/16/32 KiB CHR-ROM only. Missing address lines mirror banks.
       Legacy iNES policy matches the pinned core: no PRG bus-conflict AND.
       NES 2.0 conflict submappers and oversized CNROM are not accepted. */
    unsigned bank=value & (n->nes_rom.chr_rom_size-1u);
    (void)address;
    for (i=0;i<8;++i)
        n->nes_ppu.pattern_table[i]=n->nes_rom.chr_rom+bank*8192u+i*1024u;
}

int fm1_nes_validate_rom(const uint8_t *rom, size_t size) {
    size_t required;
    unsigned i, mapper;
    if (!rom || size < 16 || memcmp(rom, "NES\x1a", 4)) return FM1_NES_INVALID;
    /* Strict iNES 1.0, NROM/CNROM, NTSC, no trainer/save RAM.
       Reject unsupported inputs before upstream code can dereference ROM banks. */
    mapper=(rom[6]>>4)|(rom[7]&0xf0);
    if ((rom[4] != 1 && rom[4] != 2) || (rom[6] & 0x0e) || (rom[7]&0x0f))
        return FM1_NES_UNSUPPORTED;
    if (mapper==0) {
        if (rom[5]!=1) return FM1_NES_UNSUPPORTED;
    } else if (mapper==3) {
        if (rom[5]!=1 && rom[5]!=2 && rom[5]!=4) return FM1_NES_UNSUPPORTED;
    } else return FM1_NES_UNSUPPORTED;
    for (i=8; i<16; ++i) if (rom[i]) return FM1_NES_UNSUPPORTED;
    required = 16u + (size_t)rom[4]*16384u + (size_t)rom[5]*8192u;
    return size == required ? FM1_NES_OK : FM1_NES_INVALID;
}

static void update_buttons(nes_t *n) {
    uint8_t b=platform.buttons(platform.context);
    n->nes_cpu.joypad.A1=!!(b & FM1_PAD_A);
    n->nes_cpu.joypad.B1=!!(b & FM1_PAD_B);
    n->nes_cpu.joypad.SE1=!!(b & FM1_PAD_SELECT);
    n->nes_cpu.joypad.ST1=!!(b & FM1_PAD_START);
    n->nes_cpu.joypad.U1=!!(b & FM1_PAD_UP);
    n->nes_cpu.joypad.D1=!!(b & FM1_PAD_DOWN);
    n->nes_cpu.joypad.L1=!!(b & FM1_PAD_LEFT);
    n->nes_cpu.joypad.R1=!!(b & FM1_PAD_RIGHT);
}

int fm1_nes_run(const uint8_t *rom, size_t size, const fm1_nes_platform *p,
                uint32_t frames, fm1_nes_stats *out) {
    unsigned i;
    int result=fm1_nes_validate_rom(rom,size);
    if (out) memset(out,0,sizeof(*out));
    if (result) return result;
    if (!p || !p->video || !p->audio || !p->buttons || !p->frame) return FM1_NES_INVALID;
    if (active) return FM1_NES_BUSY;
    active=1; io_error=0; platform=*p; frame_limit=frames;
    memset(&machine,0,sizeof(machine)); memset(&stats,0,sizeof(stats));
    stats.state_bytes=sizeof(machine);
    machine.nes_rom.prg_rom_size=rom[4]; machine.nes_rom.chr_rom_size=rom[5];
    machine.nes_rom.mapper_number=(rom[6]>>4)|(rom[7]&0xf0);
    machine.nes_rom.mirroring_type=rom[6]&1;
    machine.nes_rom.prg_rom=(uint8_t *)(rom+16);
    machine.nes_rom.chr_rom=(uint8_t *)(rom+16+(size_t)rom[4]*16384);
    machine.nes_mapper.mapper_write=machine.nes_rom.mapper_number==3?cnrom_write:nrom_ignore_write;
    for (i=0;i<4;++i) machine.nes_cpu.prg_banks[i]=machine.nes_rom.prg_rom+(i%(rom[4]*2u))*8192u;
    for (i=0;i<8;++i) machine.nes_ppu.pattern_table[i]=machine.nes_rom.chr_rom+i*1024u;
    nes_cpu_init(&machine); nes_ppu_init(&machine); nes_apu_init(&machine);
    update_buttons(&machine);
    nes_run(&machine);
    stats.ram0=machine.nes_cpu.cpu_ram[0]; stats.ram1=machine.nes_cpu.cpu_ram[1];
    if(out) *out=stats;
    memset(&platform,0,sizeof(platform)); active=0;
    return io_error ? FM1_NES_IO_ERROR : FM1_NES_OK;
}

int nes_draw(int x1,int y1,int x2,int y2,nes_color_t *pixels) {
    int rc;
    if(io_error)return -1;
    if(x1!=0 || x2!=255 || y1<0 || y2>=240 || y2<y1) {io_error=1;machine.nes_quit=1;return -1;}
    ++stats.video_blocks;
    {FM1_PROFILE_BEGIN(FM1_PROF_VIDEO);
     rc=platform.video(platform.context,(unsigned)y1,(unsigned)(y2-y1+1),pixels);
     FM1_PROFILE_END();}
    if(rc) {
        io_error=1; machine.nes_quit=1; return -1;
    }
    return 0;
}
int nes_sound_output(uint8_t *samples,size_t count) {
    int rc;
    if(io_error)return -1;
    stats.audio_samples+=(uint32_t)count;
    {FM1_PROFILE_BEGIN(FM1_PROF_FX);
     rc=platform.audio(platform.context,samples,count);FM1_PROFILE_END();}
    if(rc) {io_error=1;machine.nes_quit=1;return -1;}
    return 0;
}
int fm1_nes_sound_channels(const uint8_t *mixed,const uint8_t *const voices[4],size_t count) {
    int rc;
    if(!platform.audio_channels)return nes_sound_output((uint8_t *)mixed,count);
    if(io_error)return -1;
    stats.audio_samples+=(uint32_t)count;
    {FM1_PROFILE_BEGIN(FM1_PROF_FX);
     rc=platform.audio_channels(platform.context,mixed,voices,count);FM1_PROFILE_END();}
    if(rc) {
        io_error=1;machine.nes_quit=1;return -1;
    }
    return 0;
}
void nes_frame(nes_t *n) {
    FM1_PROFILE_BEGIN(FM1_PROF_FRAME);
    ++stats.frames;
    if(platform.frame(platform.context,stats.frames) || (frame_limit && stats.frames>=frame_limit)) n->nes_quit=1;
    update_buttons(n);
    FM1_PROFILE_END();
}
int fm1_nes_render_frame(void) {
    return !platform.render_frame || platform.render_frame(platform.context);
}
/* Upstream convenience allocator entry points are not used by this static port. */
void *nes_malloc(int n) {(void)n;return NULL;}
void nes_free(void *p) {(void)p;}
void *nes_memcpy(void *d,const void *s,size_t n) {return memcpy(d,s,n);}
void *nes_memset(void *d,int c,size_t n) {return memset(d,c,n);}
int nes_memcmp(const void *a,const void *b,size_t n) {return memcmp(a,b,n);}
int nes_initex(nes_t *n) {(void)n;return 0;}
int nes_deinitex(nes_t *n) {(void)n;return 0;}
