/* Render actual FM1 piano audition at three pitches; no device I/O. */
#include "mod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static fm1_mod_document song;
static void put16(unsigned char *p,unsigned n){p[0]=(unsigned char)n;p[1]=(unsigned char)(n>>8);}
static void put32(unsigned char *p,unsigned n){put16(p,n);put16(p+2,n>>16);}
#define CHECK(x) do {if(!(x)){fprintf(stderr,"check failed: %s\n",#x);return 1;}}while(0)

int main(int argc,char **argv){
    FILE *in,*out;long size;unsigned char head[44]={0},bytes[512];
    int16_t audio[256];fm1_mod_player player;unsigned notes[]={48,52,55};
    unsigned frames=0,durations[3]={0},peaks[3]={0},n,i;uint32_t crc;
    size_t phrase_bytes,loop_start,loop_bytes;
    CHECK(argc==3);
    in=fopen(argv[1],"rb");CHECK(in);
    CHECK(!fseek(in,0,SEEK_END));size=ftell(in);CHECK(size>=1084 && size<=FM1_MOD_CAPACITY);
    CHECK(!fseek(in,0,SEEK_SET));CHECK(fread(song.bytes,1,size,in)==(size_t)size);fclose(in);
    CHECK(!fm1_mod_open(&song,size));crc=fm1_mod_crc32(song.bytes,song.length);
    CHECK(fm1_mod_sample(&song,7,&phrase_bytes,&loop_start,&loop_bytes));CHECK(loop_bytes<=2);
    out=fopen(argv[2],"wb");CHECK(out);CHECK(fwrite(head,1,44,out)==44);
    memset(&player,0,sizeof(player));player.document=&song;player.stereo=0;
    for(n=0;n<3;n++){
        CHECK(!fm1_mod_preview_start(&player,&song,7,notes[n],0));
        while(player.preview_active){
            CHECK(durations[n]<FM1_MOD_RATE*12);
            fm1_mod_render(&player,audio,128);
            for(i=0;i<256;i++){
                unsigned peak=audio[i]<0?-(int)audio[i]:(unsigned)audio[i];
                if(peak>peaks[n])peaks[n]=peak;put16(bytes+2*i,(uint16_t)audio[i]);
            }
            CHECK(fwrite(bytes,4,128,out)==128);frames+=128;durations[n]+=128;
        }
        CHECK(peaks[n]>0 && peaks[n]<32767);
        /* Q16 increment truncation permits a few frames over mathematical length. */
        {uint32_t step=(uint32_t)((3546895ull<<16)/((uint64_t)fm1_mod_period_for_midi(notes[n])*FM1_MOD_RATE));
         uint64_t expected=(((uint64_t)phrase_bytes<<16)+step-1)/step;
         CHECK(durations[n]>=expected && durations[n]-expected<=128);}
        memset(bytes,0,sizeof(bytes));
        for(i=0;i<FM1_MOD_RATE/128/4;i++){CHECK(fwrite(bytes,4,128,out)==128);frames+=128;}
    }
    CHECK(durations[0]>durations[1] && durations[1]>durations[2]);
    CHECK(fm1_mod_crc32(song.bytes,song.length)==crc && !song.dirty);
    /* Audition while playing is additive and leaves the song running and unedited. */
    fm1_mod_player_start(&player,&song);
    CHECK(!fm1_mod_preview_start(&player,&song,7,50,1));
    fm1_mod_render(&player,audio,128);
    CHECK(player.playing && player.preview_active && player.voices[0].active);
    CHECK(fm1_mod_crc32(song.bytes,song.length)==crc && !song.dirty);
    memcpy(head,"RIFF",4);put32(head+4,36+frames*4);memcpy(head+8,"WAVEfmt ",8);
    put32(head+16,16);put16(head+20,1);put16(head+22,2);put32(head+24,FM1_MOD_RATE);
    put32(head+28,FM1_MOD_RATE*4);put16(head+32,4);put16(head+34,16);
    memcpy(head+36,"data",4);put32(head+40,frames*4);
    CHECK(!fseek(out,0,SEEK_SET));CHECK(fwrite(head,1,44,out)==44);CHECK(!fclose(out));
    for(n=0;n<3;n++)printf("phrase MIDI%u: %.4fs peak%u\n",notes[n],(double)durations[n]/FM1_MOD_RATE,peaks[n]);
    printf("PASS: pitch/speed relationship, no clipping, unchanged song, concurrent audition\n");
    return 0;
}
