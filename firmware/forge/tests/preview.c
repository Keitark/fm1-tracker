/* Render the actual firmware LCD function into a portable offline preview. */
#include "forge.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv){
    fm1_forge_frame f={0};uint8_t wire[480*8];unsigned y,i;FILE *out;
    if(argc!=2)return 2;out=fopen(argv[1],"wb");if(!out)return 3;
    f.input.keys=(UINT64_C(1)<<14)|(UINT64_C(1)<<22)|(UINT64_C(1)<<40);
    f.input.seen=(UINT64_C(1)<<41)-1;f.input.encoder_seen=127;
    for(i=0;i<7;i++)f.input.encoder[i]=(int32_t)i*4-12;
    f.input.volume_raw=603;f.input.volume_gain=75;f.input.volume_valid=1;f.input.volume_min=0;f.input.volume_max=1023;
    f.sys_hz=480000000;f.lsb_hz=60000000;f.fps_tenths=287;f.lcd_completed=248;f.usb_audio=1;
    fprintf(out,"P6\n240 240\n255\n");
    for(y=0;y<240;y+=8){if(fm1_forge_render(&f,y,8,wire,16)){fclose(out);return 4;}
        for(i=0;i<240*8;i++){unsigned p=(wire[2*i]<<8)|wire[2*i+1];uint8_t rgb[3];
            rgb[0]=(uint8_t)(((p>>11)*255+15)/31);rgb[1]=(uint8_t)((((p>>5)&63)*255+31)/63);rgb[2]=(uint8_t)(((p&31)*255+15)/31);fwrite(rgb,1,3,out);}}
    fclose(out);return 0;
}
