#include "fm1_mdx.h"
#include "fm1_mdx_usb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
extern const unsigned char fm1_demo_mdx[],fm1_demo_pdx[];
extern const size_t fm1_demo_mdx_size,fm1_demo_pdx_size;
static fm1_mdx_player p;
static fm1_mdx_upload upload;
static int16_t stereo[256];
static char response[200];
static unsigned requested;
static int idle(void *ctx){(void)ctx;return 1;}
static int request(void *ctx,unsigned op,unsigned a,unsigned b){(void)ctx;(void)a;(void)b;requested=op;return 0;}
static void status(void *ctx,char *out,size_t n){(void)ctx;snprintf(out,n,"MDX running=0\n");}
static void reply(void *ctx,const char *s){(void)ctx;snprintf(response,sizeof(response),"%s",s);}
static void line(const char *s,unsigned now){fm1_mdx_usb_io io={0,idle,request,status};CHECK(fm1_mdx_usb_line(&upload,&io,s,now,reply,0));}
static unsigned crc32(const uint8_t *b,size_t n) {
    unsigned c=~0u,i;while(n--){c^=*b++;for(i=0;i<8;i++)c=(c>>1)^(0xedb88320u&(0u-(c&1)));}return ~c;
}
static long long audio(unsigned blocks){unsigned i,j;long long sum=0;for(i=0;i<blocks;i++){CHECK(!fm1_mdx_render(&p,stereo,128));for(j=0;j<256;j++)sum+=abs(stereo[j]);}return sum;}
int main(void) {
    unsigned i,n;uint8_t image[4096];char s[300];
    CHECK(!fm1_mdx_load(&p,fm1_demo_mdx,fm1_demo_mdx_size,fm1_demo_pdx,fm1_demo_pdx_size));
    CHECK(p.mdx.track_count==9);CHECK(audio(400)>100000);
    CHECK(fm1_mdx_mute(&p,16,1)==-1);
    CHECK(!fm1_mdx_mute(&p,0,1));
    fm1_mdx_song_write(&p,8,0x78);CHECK(p.registers[8]==0);
    fm1_mdx_song_write(&p,0x40,0x21);CHECK(p.registers[0x40]==0x21);
    CHECK(!fm1_mdx_note(&p,60,1));CHECK(p.registers[8]==0x78);
    fm1_mdx_song_write(&p,8,0);CHECK(p.registers[8]==0x78);
    CHECK(audio(100)>0);CHECK(p.live_note[0]==60);CHECK(p.suppressed_keys>1);
    CHECK(!fm1_mdx_note(&p,61,0));CHECK(p.live_note[0]==60);
    CHECK(!fm1_mdx_note(&p,60,0));CHECK(p.live_note[0]==-1);CHECK(p.registers[8]==0);
    CHECK(!fm1_mdx_note(&p,60,1));CHECK(!fm1_mdx_select(&p,1));CHECK(p.live_note[0]==-1);
    CHECK(fm1_mdx_note(&p,60,1)==-1);CHECK(!fm1_mdx_mute(&p,1,1));CHECK(!fm1_mdx_note(&p,60,1));
    CHECK(!fm1_mdx_mute(&p,1,0));CHECK(p.live_note[1]==-1);
    for(i=0;i<9;i++)CHECK(!fm1_mdx_mute(&p,i,1));
    audio(400);CHECK(audio(100)==0); /* All playback triggers gated, tails drained. */
    CHECK(!fm1_mdx_select(&p,0));CHECK(!fm1_mdx_note(&p,65,1));CHECK(audio(20)>0);
    CHECK(p.registers[0x40]==p.song_registers[0x40]);
    fm1_mdx_stop(&p);CHECK(!p.playing);for(i=0;i<8;i++)CHECK(p.live_note[i]==-1);
    CHECK(fm1_mdx_load(&p,fm1_demo_mdx,fm1_demo_mdx_size,0,0)==RETROFM_MDX_MISSING_PDX);
    memcpy(image,"FM1M",4);n=(unsigned)fm1_demo_mdx_size;memcpy(image+4,&n,4);n=(unsigned)fm1_demo_pdx_size;memcpy(image+8,&n,4);
    memcpy(image+12,fm1_demo_mdx,fm1_demo_mdx_size);memcpy(image+12+fm1_demo_mdx_size,fm1_demo_pdx,fm1_demo_pdx_size);
    n=(unsigned)(12+fm1_demo_mdx_size+fm1_demo_pdx_size);
    snprintf(s,sizeof(s),"MDX BEGIN %08x %08x",n,crc32(image,n));line(s,0);CHECK(upload.active);
    for(i=0;i<n;) {unsigned j,count=n-i;if(count>120)count=120;snprintf(s,sizeof(s),"MDX DATA %08x ",i);for(j=0;j<count;j++)snprintf(s+18+j*2,3,"%02x",image[i+j]);line(s,1);i+=count;CHECK(upload.received==i);}
    line("MDX END",2);CHECK(upload.ready && !upload.active);CHECK(upload.mdx_size==fm1_demo_mdx_size);
    line("MDX PLAY",3);CHECK(requested==FM1_MDX_PLAY);
    line("MDX MUTE 0F 1",3);CHECK(requested==FM1_MDX_MUTE);
    line("MDX NOTE 3C 1",3);CHECK(requested==FM1_MDX_NOTE);
    line("MDX BEGIN 00000010 00000000",4);line("MDX DATA 00000001 00",5);CHECK(!upload.active && !upload.ready);
    line("MDX BEGIN 00000010 00000000",6);fm1_mdx_usb_timeout(&upload,10007);CHECK(!upload.active);
    line("MDX BEGIN FFFFFFFF 00000000",8);CHECK(!upload.active);
    /* Truncated headers and every single-byte mutation of the demo must remain bounded. */
    for(i=0;i<fm1_demo_mdx_size;i++) {
        memcpy(image,fm1_demo_mdx,fm1_demo_mdx_size);image[i]^=0xff;
        if(!fm1_mdx_load(&p,image,fm1_demo_mdx_size,fm1_demo_pdx,fm1_demo_pdx_size)){
            unsigned j;for(j=0;j<5;j++)if(fm1_mdx_render(&p,stereo,128))break;
        }
    }
    for(i=0;i<40;i++)CHECK(fm1_mdx_load(&p,fm1_demo_mdx,i,0,0));
    puts("PASS stereo MDX/PDX, live ownership, parameter continuity, mute transitions, bounded USB and malformed files");return 0;
}
