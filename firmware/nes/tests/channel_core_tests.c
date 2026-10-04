#include "fm1_rom50.h"
#include "fm1_channel_fx.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static unsigned frame_no,legacy_calls,channel_calls,fail;
static uint32_t hash,raw_hash[4];
static fm1_channel_fx old_mix,loud_mix;
static double square_old,square_loud;
static unsigned measured,peak_old,peak_loud;
static int video(void *ctx,unsigned y,unsigned rows,const uint16_t *p){(void)ctx;(void)y;(void)rows;(void)p;return 0;}
static uint8_t buttons(void *ctx){(void)ctx;return frame_no>=120 && frame_no<124?FM1_PAD_START:0;}
static int frame(void *ctx,uint32_t f){(void)ctx;frame_no=f;return 0;}
static int audio(void *ctx,const uint8_t *mixed,size_t n) {
    size_t i;(void)ctx;++legacy_calls;for(i=0;i<n;++i)hash=(hash^mixed[i])*16777619u;return 0;
}
static int channels(void *ctx,const uint8_t *mixed,const uint8_t *const voice[4],size_t n) {
    size_t i;unsigned c;(void)ctx;++channel_calls;
    if(fail)return -1;
    {
        size_t at,count;const uint8_t *part[4];int16_t old_pcm[64],loud_pcm[64];
        for(at=0;at<n;at+=count) {
            count=n-at;if(count>64)count=64;
            for(c=0;c<4;c++)part[c]=voice[c]+at;
            if(fm1_channel_fx_process(&old_mix,mixed+at,part,4,old_pcm,count) ||
               fm1_channel_fx_process(&loud_mix,mixed+at,part,128,loud_pcm,count))return -1;
            for(i=0;i<count;i++) {
                if(abs(loud_pcm[i])>13952 || abs(loud_pcm[i]-32*old_pcm[i])>31)return -1;
                if(frame_no>=180) {
                    unsigned a=abs(old_pcm[i]),b=abs(loud_pcm[i]);
                    if(a>peak_old)peak_old=a;if(b>peak_loud)peak_loud=b;
                    square_old+=(double)a*a;square_loud+=(double)b*b;++measured;
                }
            }
        }
        if(loud_mix.limited)return -1;
    }
    for(i=0;i<n;++i) {
        unsigned reconstructed=(247u*(voice[0][i]+voice[1][i])+279u*voice[2][i]+162u*voice[3][i])/128u;
        if(reconstructed!=mixed[i])return -1;
        for(c=0;c<4;++c){if(voice[c][i]>15)return -1;raw_hash[c]=(raw_hash[c]^voice[c][i])*16777619u;}
        hash=(hash^mixed[i])*16777619u;
    }
    return 0;
}
int main(void) {
    size_t bytes;const uint8_t *rom=fm1_rom50_data(&bytes);fm1_nes_stats stats;
    fm1_nes_platform p={0,video,audio,buttons,frame,0};uint32_t dry_hash;
    hash=2166136261u;if(fm1_nes_run(rom,bytes,&p,600,&stats))return 1;dry_hash=hash;
    if(stats.audio_samples!=441000 || legacy_calls!=600 || channel_calls)return 2;
    p.audio_channels=channels;frame_no=legacy_calls=channel_calls=0;hash=2166136261u;
    fm1_channel_fx_reset(&old_mix);fm1_channel_fx_reset(&loud_mix);
    if(fm1_nes_run(rom,bytes,&p,600,&stats) || hash!=dry_hash || legacy_calls || channel_calls!=600)return 3;
    if(stats.audio_samples!=441000 || raw_hash[0]==raw_hash[1])return 4;
    if(!measured || square_old==0 || square_loud<1000*square_old)return 6;
    printf("SMB1 gameplay PCM16 gain4->128: peak %u->%u RMS %.2f->%.2f boost %.2fdB clipped=%u samples=%u\n",
        peak_old,peak_loud,sqrt(square_old/measured),sqrt(square_loud/measured),
        10*log10(square_loud/square_old),loud_mix.limited,measured);
    printf("SMB1 600 frames: identical mixed hash=%u; independent raw hashes=%u,%u,%u,%u\n",hash,raw_hash[0],raw_hash[1],raw_hash[2],raw_hash[3]);
    fail=1;if(fm1_nes_run(rom,bytes,&p,600,&stats)!=FM1_NES_IO_ERROR || stats.audio_samples!=735)return 5;
    puts("PASS real APU capture, exact mixer reconstruction/fallback, single delivery and error stop");return 0;
}
