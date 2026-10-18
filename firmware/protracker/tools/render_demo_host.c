/* Offline rendering through the actual tracker engine; no device or flash I/O. */
#include "mod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static fm1_mod_document document;
static void put16(uint8_t *p, unsigned v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void put32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
int main(int argc, char **argv) {
    FILE *input, *output; long length; uint8_t header[44]={0}, bytes[512]; int16_t audio[256];
    fm1_mod_player player; uint32_t frames=0, limit=FM1_MOD_RATE*64u, nonzero[2]={0};
    int entered_later_order=0, finished=0, peak=0; unsigned i, channel;
    if(argc!=3){fprintf(stderr,"Usage: mod_demo_preview SONG.MOD OUTPUT.wav\n");return 2;}
    input=fopen(argv[1],"rb");if(!input)return 3;
    if(fseek(input,0,SEEK_END)){fclose(input);return 3;}length=ftell(input);
    if(length<1084 || length>FM1_MOD_CAPACITY || fseek(input,0,SEEK_SET)){fclose(input);return 3;}
    if(fread(document.bytes,1,(size_t)length,input)!=(size_t)length){fclose(input);return 3;}fclose(input);
    if(fm1_mod_open(&document,(size_t)length))return 4;
    output=fopen(argv[2],"wb");if(!output)return 5;
    if(fwrite(header,1,44,output)!=44){fclose(output);return 5;}
    fm1_mod_player_start(&player,&document);player.stereo=1;
    while(frames<limit && !finished){
        /* One frame near the final order catches the loop before the new intro sounds. */
        unsigned count=player.current_order+1==document.orders?1:128;
        fm1_mod_render(&player,audio,count);
        if(player.current_order)entered_later_order=1;
        if(entered_later_order && !player.current_order && !player.current_row){finished=1;break;}
        for(i=0;i<count;++i)for(channel=0;channel<2;++channel){
            int value=audio[i*2+channel], absolute=value<0?-value:value;
            if(value)nonzero[channel]++;if(absolute>peak)peak=absolute;
            put16(bytes+(i*2+channel)*2,(unsigned)(uint16_t)value);
        }
        if(fwrite(bytes,4,count,output)!=count){fclose(output);return 5;}frames+=count;
    }
    memcpy(header,"RIFF",4);put32(header+4,36+frames*4);memcpy(header+8,"WAVEfmt ",8);
    put32(header+16,16);put16(header+20,1);put16(header+22,2);put32(header+24,FM1_MOD_RATE);
    put32(header+28,FM1_MOD_RATE*4);put16(header+32,4);put16(header+34,16);
    memcpy(header+36,"data",4);put32(header+40,frames*4);
    if(fseek(output,0,SEEK_SET) || fwrite(header,1,44,output)!=44){fclose(output);return 5;}
    fclose(output);printf("%u frames, %.3f seconds, peak%d, L/R active frames%u/%u, arrangement complete=%d\n",
        frames,(double)frames/FM1_MOD_RATE,peak,nonzero[0],nonzero[1],finished);
    return finished && nonzero[0] && nonzero[1]?0:6;
}
