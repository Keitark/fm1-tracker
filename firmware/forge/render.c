#include "forge.h"
#include <stdio.h>
#include <string.h>
/* Original3x5 glyphs. Only a strip buffer is constructed here; the LCD HAL
 * copies it into its own aligned frame before return. No full screen allocation. */
static const uint8_t font[96][5]={
 [3]={2,7,5,7,2},[11]={0,2,7,2,0},[13]={0,0,7,0,0},[14]={0,0,0,0,2},[15]={1,1,2,4,4},
 [16]={7,5,5,5,7},[17]={2,6,2,2,7},[18]={7,1,7,4,7},[19]={7,1,7,1,7},[20]={5,5,7,1,1},
 [21]={7,4,7,1,7},[22]={7,4,7,5,7},[23]={7,1,1,2,2},[24]={7,5,7,5,7},[25]={7,5,7,1,7},
 [26]={0,2,0,2,0},[30]={4,2,1,2,4},
 [33]={2,5,7,5,5},[34]={6,5,6,5,6},[35]={7,4,4,4,7},[36]={6,5,5,5,6},[37]={7,4,6,4,7},
 [38]={7,4,6,4,4},[39]={7,4,5,5,7},[40]={5,5,7,5,5},[41]={7,2,2,2,7},[42]={1,1,1,5,7},
 [43]={5,5,6,5,5},[44]={4,4,4,4,7},[45]={5,7,7,5,5},[46]={5,7,7,7,5},[47]={7,5,5,5,7},
 [48]={7,5,7,4,4},[49]={7,5,5,7,1},[50]={6,5,6,5,5},[51]={7,4,7,1,7},[52]={7,2,2,2,2},
 [53]={5,5,5,5,7},[54]={5,5,5,5,2},[55]={5,5,7,7,5},[56]={5,5,2,5,5},[57]={5,5,2,2,2},[58]={7,1,2,4,7}};
enum {BLACK=0,INK=0x2104,DIM=0x4a69,MINT=0x6ff3,GREEN=0x47e8,WHITE=0xffff,RED=0xf986};
static void bar(uint16_t row[240],unsigned y,unsigned x,unsigned top,unsigned w,unsigned h,unsigned color){
    unsigned i;if(y<top || y>=top+h || x>=240)return;if(w>240-x)w=240-x;
    for(i=x;i<x+w;i++)row[i]=(uint16_t)color;
}
static void text(uint16_t row[240],unsigned y,unsigned x,unsigned top,const char *s,unsigned color,unsigned scale){
    unsigned line,i,j;if(y<top || y>=top+5*scale || !scale)return;line=(y-top)/scale;
    for(;*s && x<240;s++,x+=4*scale){unsigned c=(unsigned char)*s;if(c>='a' && c<='z')c-=32;
        if(c<32 || c>127)continue;for(i=0;i<3;i++)if(font[c-32][line]&(4u>>i))for(j=0;j<scale;j++)if(x+i*scale+j<240)row[x+i*scale+j]=(uint16_t)color;}
}
static unsigned bits(uint64_t m){unsigned n=0;while(m){n+=(unsigned)(m&1);m>>=1;}return n;}
int fm1_forge_render(const fm1_forge_frame *f,unsigned top,unsigned rows,uint8_t *wire,unsigned bpp){
    static const char *const enc[7]={"SELECT","ALGORITHM","KNOB1","KNOB2","KNOB3","KNOB4","PRESETS"};
    unsigned y,x,i;char coverage[48],metrics[64],clock[48],volume[48],counts[7][48];
    if(!f || !wire || !rows || rows>8 || top>=240 || rows>240-top || (bpp!=12 && bpp!=16))return -1;
    snprintf(coverage,sizeof(coverage),"KEYS %u/41 ENC %u/7",bits(f->input.seen),bits(f->input.encoder_seen));
    snprintf(metrics,sizeof(metrics),"FPS %u.%u LCD %lu ERR %lu",f->fps_tenths/10,f->fps_tenths%10,(unsigned long)f->lcd_completed,(unsigned long)f->lcd_errors);
    snprintf(clock,sizeof(clock),"CPU %luM BUS %luM",(unsigned long)(f->sys_hz/1000000),(unsigned long)(f->lsb_hz/1000000));
    snprintf(volume,sizeof(volume),"VOLUME %u GAIN %u RANGE %u-%u",f->input.volume_raw,f->input.volume_gain,f->input.volume_min,f->input.volume_max);
    for(i=0;i<7;i++)snprintf(counts[i],sizeof(counts[i]),"%-9s %ld BAD %lu",enc[i],(long)f->input.encoder[i],(unsigned long)f->input.encoder_invalid[i]);
    for(y=top;y<top+rows;y++){
        uint16_t row[240];memset(row,0,sizeof(row));
        text(row,y,6,3,"FM1 FORGE DIAGNOSTICS",MINT,2);
        for(i=0;i<41;i++){
            unsigned slot=i<14?i:i-14;unsigned col=i<14?slot%7:slot%9;
            unsigned keyy=i<14?19+(slot/7)*13:48+(slot/9)*13;
            unsigned keyx=6+col*(i<14?33:25),w=i<14?31:23;
            unsigned color=(f->input.keys&(UINT64_C(1)<<i))?GREEN:(f->input.seen&(UINT64_C(1)<<i))?MINT:INK;
            bar(row,y,keyx,keyy,w,11,color);
            text(row,y,keyx+2,keyy+3,fm1_forge_key_name(i),(color==INK)?WHITE:BLACK,1);
        }
        text(row,y,6,89,coverage,WHITE,1);
        for(i=0;i<7;i++){bar(row,y,6,99+i*9,4,5,(f->input.encoder_seen&(1u<<i))?MINT:DIM);text(row,y,15,99+i*9,counts[i],WHITE,1);}
        text(row,y,6,164,volume,f->input.volume_valid?MINT:RED,1);
        bar(row,y,6,172,226,8,INK);if(f->input.volume_valid)bar(row,y,6,172,(unsigned)f->input.volume_raw*226/1023,8,MINT);
        text(row,y,6,185,metrics,WHITE,1);text(row,y,6,195,clock,WHITE,1);
        bar(row,y,6,207,101,24,INK);bar(row,y,132,207,102,24,INK);
        text(row,y,11,211,"SYSTEM / USB",MINT,1);text(row,y,11,221,f->usb_audio?"CDC UAC UBOOT":"CDC UBOOT",WHITE,1);
        text(row,y,115,217,">",MINT,2);text(row,y,137,211,"USER MODULE",MINT,1);
        text(row,y,137,221,f->module_user?"STARTER":"DIAGNOSTICS",WHITE,1);
        text(row,y,6,234,f->fault?"FAULT: CHECK FORGE STATUS":"KEYS: QUIET TRIANGLE / 2S CAP",f->fault?RED:DIM,1);
        if(bpp==16)for(x=0;x<240;x++){wire[2*x]=(uint8_t)(row[x]>>8);wire[2*x+1]=(uint8_t)row[x];}
        else for(x=0;x<240;x+=2){uint16_t a=row[x],b=row[x+1];unsigned p=x*3/2;
            wire[p]=(uint8_t)((a>>8)&0xf0)|((a>>7)&15);wire[p+1]=(uint8_t)((a&0x1e)<<3)|((b>>12)&15);wire[p+2]=(uint8_t)((b>>3)&0xf0)|((b>>1)&15);}
        wire+=bpp==16?480:360;
    }return 0;
}
