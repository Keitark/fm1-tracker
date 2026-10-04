#include "protocol.h"
#include "fm1_mdx.h"
#include "fm1_mdx_usb.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
static fm1_mdx_upload upload;
static fm1_mdx_player player;
static int idle(void *u){(void)u;return !player.playing;}
static int request(void *u,unsigned op,unsigned a,unsigned b){(void)u;
    if(op==FM1_MDX_STOP)fm1_mdx_stop(&player);
    if(op==FM1_MDX_PLAY)return fm1_mdx_load(&player,upload.bytes+12,upload.mdx_size,upload.bytes+12+upload.mdx_size,upload.pdx_size);
    if(op==FM1_MDX_SELECT)return fm1_mdx_select(&player,a);
    if(op==FM1_MDX_MUTE)return fm1_mdx_mute(&player,a,(int)b);
    if(op==FM1_MDX_NOTE)return fm1_mdx_note(&player,a,(int)b);
    return 0;
}
static void status(void *u,char *out,size_t n){(void)u;snprintf(out,n,"MDX running=%u pending=0 ready=%u received=%u\n",player.playing,upload.ready,upload.received);}
int fm1_mdx_usb_command(const char *s,uint32_t now,fm1_mdx_reply reply,void *ctx){fm1_mdx_usb_io io={0,idle,request,status};return fm1_mdx_usb_line(&upload,&io,s,now,reply,ctx);}
void fm1_mdx_usb_reset(void){fm1_mdx_usb_abort(&upload);}
void fm1_mdx_usb_tick(uint32_t now){fm1_mdx_usb_timeout(&upload,now);}
static void output(void *ctx,const char *s){(void)ctx;fputs(s,stdout);fflush(stdout);}
int main(void){int c;fm1_diag_protocol p;
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    fm1_diag_reset(&p);while((c=getchar())!=EOF){uint8_t b=(uint8_t)c;fm1_diag_feed(&p,&b,1,0,output,0);}return 0;
}
