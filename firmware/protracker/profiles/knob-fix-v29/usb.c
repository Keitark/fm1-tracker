#include "usb.h"
#include <stdio.h>
#include <string.h>

static int digit(char c){
    if(c>='0' && c<='9')return c-'0';
    if(c>='a' && c<='f')return c-'a'+10;
    if(c>='A' && c<='F')return c-'A'+10;
    return -1;
}
static int hex(const char *s,unsigned n,uint32_t *v){
    unsigned i;*v=0;for(i=0;i<n;i++){int x=digit(s[i]);if(x<0)return 0;*v=(*v<<4)|(unsigned)x;}return 1;
}
static uint32_t crc_byte(uint32_t c,uint8_t b){
    unsigned i;c^=b;for(i=0;i<8;i++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));return c;
}
void fm1_mod_usb_abort(fm1_mod_transfer *t){if(t){t->active=0;t->exporting=0;t->received=0;t->export_length=0;}}
void fm1_mod_usb_timeout(fm1_mod_transfer *t,uint32_t now){
    if(t && (t->active || t->exporting) && (uint32_t)(now-t->last_ms)>10000u)fm1_mod_usb_abort(t);
}
static void bad(fm1_mod_transfer *t,fm1_mod_reply reply,void *context){
    fm1_mod_usb_abort(t);reply(context,"ERR MOD PROTOCOL_OR_CRC\n");
}
int fm1_mod_usb_line(fm1_mod_document *d,fm1_mod_transfer *t,const fm1_mod_usb_io *io,
                    const char *s,uint32_t now,fm1_mod_reply reply,void *context){
    size_t n,i;uint32_t a,b;char out[192];
    if(strncmp(s,"MOD ",4))return 0;
    if(!d || !t || !io || !s || !reply)return 1;
    n=strlen(s);fm1_mod_usb_timeout(t,now);
    if(!strcmp(s,"MOD STATUS")){io->status(io->context,out,sizeof(out));reply(context,out);return 1;}
    if(!strcmp(s,"MOD ABORT")){fm1_mod_usb_abort(t);reply(context,"OK MOD ABORT\n");return 1;}
    if(!strcmp(s,"MOD STOP") || !strcmp(s,"MOD PLAY") || !strcmp(s,"MOD NEW")){
        unsigned op=s[4]=='S'?FM1_MOD_REQUEST_STOP:s[4]=='P'?FM1_MOD_REQUEST_PLAY:FM1_MOD_REQUEST_NEW;
        if(t->active || t->exporting || (op==FM1_MOD_REQUEST_NEW && !io->idle(io->context)) ||
           (op==FM1_MOD_REQUEST_PLAY && (!d->loaded || !io->idle(io->context)))){
            reply(context,"ERR MOD BUSY_OR_NO_SONG\n");return 1;
        }
        if(io->request(io->context,op))reply(context,"ERR MOD BUSY\n");
        else reply(context,"OK MOD QUEUED\n");return 1;
    }
    if(n==27 && !memcmp(s,"MOD BEGIN ",10) && s[18]==' ' && hex(s+10,8,&a) && hex(s+19,8,&b)){
        if(t->active || t->exporting || !io->idle(io->context)){reply(context,"ERR MOD STOP_FIRST\n");return 1;}
        if(a<2108 || a>FM1_MOD_CAPACITY){reply(context,"ERR MOD SIZE\n");return 1;}
        d->loaded=0;d->dirty=0;t->length=a;t->received=0;t->expected_crc=b;
        t->crc=0xffffffffu;t->active=1;t->last_ms=now;
        reply(context,"OK MOD BEGIN RAM\n");return 1;
    }
    if(n>=20 && n<=146 && !memcmp(s,"MOD DATA ",9) && s[17]==' ' && !(n&1) && hex(s+9,8,&a)){
        size_t count=(n-18)/2;
        if(!t->active || a!=t->received || !count || count>64 || count>t->length-t->received)goto invalid;
        for(i=18;i<n;i++)if(digit(s[i])<0)goto invalid;
        for(i=18;i<n;i+=2){uint8_t value=(uint8_t)((digit(s[i])<<4)|digit(s[i+1]));
            d->bytes[t->received++]=value;t->crc=crc_byte(t->crc,value);}
        t->last_ms=now;snprintf(out,sizeof(out),"OK MOD DATA %08lx\n",(unsigned long)t->received);
        reply(context,out);return 1;
    }
    if(!strcmp(s,"MOD END")){
        if(!t->active || t->received!=t->length || (t->crc^0xffffffffu)!=t->expected_crc ||
           fm1_mod_open(d,t->length))goto invalid;
        t->active=0;reply(context,"OK MOD READY RAM VOLATILE\n");return 1;
    }
    if(!strcmp(s,"MOD INFO")){
        if(t->active || !d->loaded || !io->idle(io->context)){
            reply(context,"ERR MOD STOP_FIRST\n");return 1;}
        if(!t->exporting || t->export_length!=d->length){
            t->export_crc=fm1_mod_crc32(d->bytes,d->length);t->export_length=d->length;
        }
        t->exporting=1;t->last_ms=now;
        snprintf(out,sizeof(out),"MOD INFO %08lx %08lx %u\n",
                 (unsigned long)d->length,(unsigned long)t->export_crc,d->dirty);
        reply(context,out);return 1;
    }
    if(n==20 && !memcmp(s,"MOD READ ",9) && s[17]==' ' && hex(s+9,8,&a) && hex(s+18,2,&b)){
        static const char *digits="0123456789abcdef";unsigned k;
        if(t->active || !t->exporting || !d->loaded || !io->idle(io->context)){
            reply(context,"ERR MOD STOP_FIRST\n");return 1;}
        if(!b || b>64 || a>d->length || b>d->length-a)goto invalid;
        memcpy(out,"MOD DATA ",9);
        for(k=0;k<8;k++)out[9+k]=digits[(a>>(28-4*k))&15];
        out[17]=' ';
        for(k=0;k<b;k++){uint8_t v=d->bytes[a+k];out[18+2*k]=digits[v>>4];out[19+2*k]=digits[v&15];}
        out[18+2*b]='\n';out[19+2*b]=0;t->last_ms=now;reply(context,out);return 1;
    }
    if(n==18 && !memcmp(s,"MOD SAVED ",10) && hex(s+10,8,&a)){
        if(!t->exporting || !d->loaded || t->export_length!=d->length || a!=t->export_crc)goto invalid;
        t->exporting=0;t->export_length=0;d->dirty=0;reply(context,"OK MOD SAVED\n");return 1;
    }
invalid:
    bad(t,reply,context);return 1;
}
