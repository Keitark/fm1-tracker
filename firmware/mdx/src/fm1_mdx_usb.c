#include "fm1_mdx_usb.h"
#include "retrofm_mdx.h"
#include "retrofm_pdx.h"
#include <stdio.h>
#include <string.h>
static int digit(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
static int hex(const char *s,uint32_t *v) {
    unsigned i;*v=0;
    for(i=0;i<8;i++){int d=digit(s[i]);if(d<0)return 0;*v=(*v<<4)|(unsigned)d;}
    return 1;
}
static uint32_t crc_byte(uint32_t c,uint8_t b) {
    unsigned i;c^=b;for(i=0;i<8;i++)c=(c>>1)^(0xedb88320u&(0u-(c&1)));return c;
}
static uint32_t le32(const uint8_t *b){return b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);}
void fm1_mdx_usb_abort(fm1_mdx_upload *u){u->active=0;u->received=0;}
void fm1_mdx_usb_timeout(fm1_mdx_upload *u,uint32_t now){if(u->active && (uint32_t)(now-u->last_ms)>10000)fm1_mdx_usb_abort(u);}
int fm1_mdx_usb_line(fm1_mdx_upload *u,const fm1_mdx_usb_io *io,const char *s,uint32_t now,fm1_mdx_reply reply,void *ctx) {
    size_t n=strlen(s),i;uint32_t a,b;char out[180];unsigned op=0;
    if(strncmp(s,"MDX ",4))return 0;
    fm1_mdx_usb_timeout(u,now);
    if(!strcmp(s,"MDX STATUS")){io->status(io->context,out,sizeof(out));reply(ctx,out);return 1;}
    if(!strcmp(s,"MDX ABORT")){fm1_mdx_usb_abort(u);reply(ctx,"OK MDX ABORT\n");return 1;}
    if(n==27 && !memcmp(s,"MDX BEGIN ",10) && s[18]==' ' && hex(s+10,&a) && hex(s+19,&b)) {
        if(u->active || !io->idle(io->context)){reply(ctx,"ERR MDX BUSY STOP_FIRST\n");return 1;}
        if(a<12 || a>FM1_MDX_UPLOAD_LIMIT){reply(ctx,"ERR MDX SIZE\n");return 1;}
        u->ready=0;u->length=a;u->received=0;u->expected=b;u->crc=0xffffffffu;u->active=1;u->last_ms=now;
        reply(ctx,"OK MDX BEGIN RAM\n");return 1;
    }
    if(n>=20 && n<=276 && !memcmp(s,"MDX DATA ",9) && s[17]==' ' && !(n&1) && hex(s+9,&a)) {
        size_t count=(n-18)/2;
        if(!u->active || a!=u->received || count>u->length-u->received)goto bad;
        for(i=18;i<n;i++)if(digit(s[i])<0)goto bad;
        for(i=18;i<n;i+=2){uint8_t v=(uint8_t)((digit(s[i])<<4)|digit(s[i+1]));u->bytes[u->received++]=v;u->crc=crc_byte(u->crc,v);}
        u->last_ms=now;snprintf(out,sizeof(out),"OK MDX DATA %08lx\n",(unsigned long)u->received);reply(ctx,out);return 1;
    }
    if(!strcmp(s,"MDX END")) {
        retrofm_mdx mdx;retrofm_pdx pdx;
        if(!u->active || u->received!=u->length || (u->crc^0xffffffffu)!=u->expected || memcmp(u->bytes,"FM1M",4))goto bad;
        a=le32(u->bytes+4);b=le32(u->bytes+8);
        if(!a || a>u->length-12 || b!=u->length-12-a)goto bad;
        if(retrofm_mdx_open(&mdx,u->bytes+12,a)!=RETROFM_MDX_OK)goto bad;
        if(mdx.uses_pdx && (!b || retrofm_pdx_open(&pdx,u->bytes+12+a,b)!=RETROFM_PDX_OK))goto bad;
        u->mdx_size=a;u->pdx_size=b;u->ready=1;u->active=0;
        reply(ctx,"OK MDX STORED RAM VOLATILE\n");return 1;
    }
    if(!strcmp(s,"MDX STOP"))op=FM1_MDX_STOP;
    if(!strcmp(s,"MDX PLAY"))op=FM1_MDX_PLAY;
    if(!strcmp(s,"MDX DEMO"))op=FM1_MDX_DEMO;
    a=b=0;
    if(n==12 && !memcmp(s,"MDX SELECT ",11) && s[11]>='0' && s[11]<='7'){op=FM1_MDX_SELECT;a=(unsigned)(s[11]-'0');}
    if(n==13 && !memcmp(s,"MDX MUTE ",9)) {
        /* Two hex digits identify tracks00..0F; the last digit is0 or1. */
        int hi=digit(s[9]),lo=digit(s[10]);
        if(hi!=0 || lo<0 || s[11]!=' ' || (s[12]!='0' && s[12]!='1'))goto bad;
        op=FM1_MDX_MUTE;a=(unsigned)lo;b=(unsigned)(s[12]-'0');
    }
    if(n==13 && !memcmp(s,"MDX NOTE ",9)) {
        int hi=digit(s[9]),lo=digit(s[10]);
        if(hi<0 || lo<0 || s[11]!=' ' || (s[12]!='0' && s[12]!='1'))goto bad;
        op=FM1_MDX_NOTE;a=(unsigned)(hi*16+lo);b=(unsigned)(s[12]-'0');if(a<13 || a>108)goto bad;
    }
    if(!op)goto bad;
    if(u->active){reply(ctx,"ERR MDX UPLOAD_ACTIVE\n");return 1;}
    if(op==FM1_MDX_PLAY && !u->ready){reply(ctx,"ERR MDX NO_UPLOAD USE_DEMO\n");return 1;}
    if(io->request(io->context,op,a,b))reply(ctx,"ERR MDX BUSY\n");else reply(ctx,"OK MDX QUEUED\n");
    return 1;
bad:
    fm1_mdx_usb_abort(u);reply(ctx,"ERR MDX PROTOCOL_FILE_OR_CRC\n");return 1;
}
