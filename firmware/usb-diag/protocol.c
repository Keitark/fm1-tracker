/* USB update TRANSPORT qualification. No flash API or received-code execution.
 * A successful END proves only length/CRC transfer, not an installable image.
 * COMMIT is deliberately fail-closed until a stock-layout writer is audited.
 */
#include "protocol.h"
#include <string.h>
#include <stdio.h>
#ifdef FM1_FORGE
extern int fm1_forge_usb_command(const char *,uint32_t,fm1_diag_reply,void *);
#endif
#ifdef FM1_MDX_PLAYER
#include "fm1_mdx_usb.h"
#endif
#ifdef FM1_MOD_PLAYER
#include "usb.h"
#endif

void fm1_diag_reset(fm1_diag_protocol *p) {
    memset(p, 0, sizeof(*p));
#ifdef FM1_MDX_PLAYER
    fm1_mdx_usb_reset();
#endif
#ifdef FM1_MOD_PLAYER
    fm1_mod_usb_reset();
#endif
}
static int digit(char c) {
    if(c>='0' && c<='9') return c-'0';
    if(c>='a' && c<='f') return c-'a'+10;
    if(c>='A' && c<='F') return c-'A'+10;
    return -1;
}
static int hex32(const char *s, uint32_t *v) {
    unsigned i; *v=0;
    for(i=0;i<8;i++) { int d=digit(s[i]); if(d<0)return 0; *v=(*v<<4)|(unsigned)d; }
    return 1;
}
static uint32_t crc_byte(uint32_t c, uint8_t b) {
    unsigned i; c^=b;
    for(i=0;i<8;i++)c=(c>>1)^(0xedb88320u & (0u-(c&1)));
    return c;
}
static void command(fm1_diag_protocol *p, uint32_t now, fm1_diag_reply reply, void *ctx, int final) {
    char out[64]; uint32_t a,b; size_t n=strlen(p->line),i;
#ifdef FM1_MDX_PLAYER
    if(!p->active && fm1_mdx_usb_command(p->line,now,reply,ctx))return;
#endif
#ifdef FM1_MOD_PLAYER
    if(!p->active && fm1_mod_usb_command(p->line,now,reply,ctx))return;
#endif
    if(!strcmp(p->line,"HELLO")) {
#ifdef FM1_FORGE
        reply(ctx,"FM1DIAG/1 FM1-FORGE/1 UBOOT=SERIAL COMMIT=BLOCKED\n");return;
#else
#ifdef FM1_PERIPHERAL_TESTS
#ifdef FM1_MDX_PLAYER
        reply(ctx,"FM1DIAG/1 MDX-KARAOKE/1 RAM-UPLOAD UBOOT=SERIAL COMMIT=BLOCKED\n");return;
#elif defined(FM1_MOD_PLAYER)
        reply(ctx,"FM1DIAG/1 MOD-EDITOR/1 RAM-TRANSFER UBOOT=SERIAL COMMIT=BLOCKED\n");return;
#elif defined(FM1_NES_PLAYER)
        reply(ctx,"FM1DIAG/1 PERIPHERAL-TEST/1 LCD=STOCK-SEQ NES=SMB1 UBOOT=SERIAL UPDATE=VERIFY-ONLY COMMIT=BLOCKED\n"); return;
#elif defined(FM1_LCD_STOCK_SEQUENCE)
        reply(ctx,"FM1DIAG/1 PERIPHERAL-TEST/1 LCD=STOCK-SEQ UBOOT=SERIAL UPDATE=VERIFY-ONLY COMMIT=BLOCKED\n"); return;
#elif defined(FM1_LCD_STOCK_DMA)
        reply(ctx,"FM1DIAG/1 PERIPHERAL-TEST/1 LCD=STOCK-DMA UBOOT=SERIAL UPDATE=VERIFY-ONLY COMMIT=BLOCKED\n"); return;
#elif defined(FM1_LCD_STOCK_FILL)
        reply(ctx,"FM1DIAG/1 PERIPHERAL-TEST/1 LCD=STOCK-FILL UBOOT=SERIAL UPDATE=VERIFY-ONLY COMMIT=BLOCKED\n"); return;
#else
        reply(ctx,"FM1DIAG/1 PERIPHERAL-TEST/1 UBOOT=SERIAL UPDATE=VERIFY-ONLY COMMIT=BLOCKED\n"); return;
#endif
#else
        reply(ctx,"FM1DIAG/1 USB-ONLY UPDATE=VERIFY-ONLY COMMIT=BLOCKED\n"); return;
#endif
#endif
    }
#ifdef FM1_FORGE
    if((!p->active && final) && fm1_forge_usb_command(p->line,now,reply,ctx))return;
#endif
    if(!strcmp(p->line,"STATUS")) {
        snprintf(out,sizeof(out),"STATUS active=%u verified=%u bytes=%lu\n",
                 p->active,p->verified,(unsigned long)p->received); reply(ctx,out); return;
    }
    if(!strcmp(p->line,"UBOOT")) {
        if(p->active || !final) {fm1_diag_reset(p);reply(ctx,"ERR UBOOT_BUSY_OR_TRAILING\n");return;}
        fm1_diag_reset(p);p->boot_requested=1;return;
    }
#ifdef FM1_PERIPHERAL_TESTS
    if(!strcmp(p->line,"HELP")) {
#ifdef FM1_FORGE
        fm1_forge_usb_command("FORGE HELP",now,reply,ctx);return;
#elif defined(FM1_NES_PLAYER)
        reply(ctx,"TEST NES | TEST STOP | TEST STATUS; stop NES before other peripheral tests; UBOOT then UBOOT CONFIRM; LF-only\n");return;
#elif defined(FM1_MOD_PLAYER)
        reply(ctx,"MOD STATUS | MOD NEW | MOD PLAY | MOD STOP | MOD BEGIN/DATA/END | MOD INFO/READ/SAVED | MOD ABORT; UBOOT then UBOOT CONFIRM; LF-only\n");return;
#else
        reply(ctx,"TEST LCD | TEST AUDIO | TEST KEYS | TEST KNOBS | TEST STOP | TEST STATUS; UBOOT then UBOOT CONFIRM; LF-only\n");return;
#endif
    }
    if(!strncmp(p->line,"TEST ",5)) {
        static const char *names[]={"STOP","LCD","AUDIO","KEYS","KNOBS","STATUS"
#ifdef FM1_NES_PLAYER
            ,"NES"
#endif
        };
        unsigned test;
        if(p->active || !final) {fm1_diag_reset(p);reply(ctx,"ERR TEST_BUSY_OR_TRAILING\n");return;}
        for(test=0;test<sizeof(names)/sizeof(names[0]);test++)if(!strcmp(p->line+5,names[test])) {
            fm1_diag_reset(p);p->test_requested=test+1;return;
        }
    }
#endif
    if(!strcmp(p->line,"ABORT")) { fm1_diag_reset(p); reply(ctx,"OK ABORT\n"); return; }
    if(!strcmp(p->line,"COMMIT")) {
        fm1_diag_reset(p); reply(ctx,"ERR COMMIT_BLOCKED STOCK_LAYOUT_UNQUALIFIED\n"); return;
    }
    if(n==23 && !memcmp(p->line,"BEGIN ",6) && p->line[14]==' ' &&
       hex32(p->line+6,&a) && hex32(p->line+15,&b)) {
        if(p->active) { fm1_diag_reset(p); reply(ctx,"ERR BUSY_ABORTED\n"); return; }
        fm1_diag_reset(p);
        if(!a || a>FM1_DIAG_LIMIT) { reply(ctx,"ERR SIZE\n"); return; }
        p->length=a; p->expected_crc=b; p->crc=0xffffffffu;
        p->active=1; p->last_ms=now; reply(ctx,"OK BEGIN VERIFY-ONLY\n"); return;
    }
    if(n>=16 && n<=270 && !memcmp(p->line,"DATA ",5) && p->line[13]==' ' &&
       !(n&1) && hex32(p->line+5,&a)) {
        size_t count=(n-14)/2;
        if(!p->active || a!=p->received || count>p->length-p->received)goto bad;
        /* Validate the complete packet before changing the rolling CRC. */
        for(i=14;i<n;i++)if(digit(p->line[i])<0)goto bad;
        for(i=14;i<n;i+=2)p->crc=crc_byte(p->crc,(uint8_t)((digit(p->line[i])<<4)|digit(p->line[i+1])));
        p->received+=(uint32_t)count; p->last_ms=now;
        snprintf(out,sizeof(out),"OK DATA %08lx\n",(unsigned long)p->received); reply(ctx,out); return;
    }
    if(!strcmp(p->line,"END")) {
        if(!p->active || p->received!=p->length || (p->crc^0xffffffffu)!=p->expected_crc)goto bad;
        p->active=0; p->verified=1;
        reply(ctx,"OK VERIFIED TRANSPORT-ONLY NOT-STORED NOT-INSTALLED\n"); return;
    }
bad:
    fm1_diag_reset(p); reply(ctx,"ERR PROTOCOL_OR_CRC ABORTED\n");
}
void fm1_diag_tick(fm1_diag_protocol *p, uint32_t now, fm1_diag_reply reply, void *ctx) {
#ifdef FM1_MDX_PLAYER
    fm1_mdx_usb_tick(now);
#endif
#ifdef FM1_MOD_PLAYER
    fm1_mod_usb_tick(now);
#endif
    if((p->active || p->used || p->dropping) && (uint32_t)(now-p->last_ms)>10000u) {
        fm1_diag_reset(p); reply(ctx,"ERR TIMEOUT ABORTED\n");
    }
}
void fm1_diag_feed(fm1_diag_protocol *p, const uint8_t *data, size_t n,
                   uint32_t now, fm1_diag_reply reply, void *ctx) {
    size_t i;
    fm1_diag_tick(p,now,reply,ctx);
    for(i=0;i<n;i++) {
        uint8_t c=data[i];
        if(c=='\n') {
            if(!p->dropping) { p->line[p->used]=0; command(p,now,reply,ctx,i+1==n); }
            else { fm1_diag_reset(p); reply(ctx,"ERR LINE ABORTED\n"); }
            p->used=0; p->dropping=0;
        } else if(c=='\r') {
#ifdef FM1_MDX_PLAYER
            fm1_mdx_usb_reset();
#endif
#ifdef FM1_MOD_PLAYER
            fm1_mod_usb_reset();
#endif
            /* The wire protocol uses LF only; fail closed on CR/CRLF. */
            p->dropping=1; p->active=p->verified=0;
        } else if(c<32 || c>126 || p->used>=sizeof(p->line)-1) {
#ifdef FM1_MDX_PLAYER
            fm1_mdx_usb_reset();
#endif
#ifdef FM1_MOD_PLAYER
            fm1_mod_usb_reset();
#endif
            p->dropping=1; p->active=p->verified=0;
        } else if(!p->dropping) {
            if(!p->used && !p->active)p->last_ms=now;
            p->line[p->used++]=(char)c;
        }
    }
}
