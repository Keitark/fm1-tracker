#include "protocol.h"
#include "usb_control.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
static char response[1024];
static unsigned replies;
static void reply(void *ctx,const char *s) {
    (void)ctx; CHECK(strlen(s)<sizeof(response));strcpy(response,s);++replies;
}
static void feed(fm1_diag_protocol *p,const char *s,uint32_t t) {
    fm1_diag_feed(p,(const uint8_t *)s,strlen(s),t,reply,NULL);
}
static void valid(fm1_diag_protocol *p) {
    feed(p,"BEGIN 00000009 cbf43926\n",10);CHECK(p->active);
    feed(p,"DATA 00000000 313233343536373839\n",20);CHECK(p->received==9);
    feed(p,"END\n",30);CHECK(p->verified && !p->active);
    CHECK(strstr(response,"NOT-STORED NOT-INSTALLED"));
}
int main(void) {
    fm1_diag_protocol p;unsigned i,t,r;char big[600];uint32_t random=1;
    fm1_diag_reset(&p);feed(&p,"HELLO\n",0);CHECK(strstr(response,"COMMIT=BLOCKED"));
    feed(&p,"UBOOT\n",0);CHECK(p.boot_requested);
    fm1_diag_reset(&p);feed(&p,"UBOOT\nHELLO\n",0);CHECK(!p.boot_requested);
    feed(&p,"BEGIN 00000001 00000000\n",0);feed(&p,"UBOOT\n",0);CHECK(!p.boot_requested && !p.active);
    fm1_diag_reset(&p);feed(&p,"UBOOT\r\n",0);CHECK(!p.boot_requested);
#ifdef FM1_PERIPHERAL_TESTS
    {
        const char *commands[]={"TEST STOP\n","TEST LCD\n","TEST AUDIO\n","TEST KEYS\n","TEST KNOBS\n","TEST STATUS\n"};
        for(i=0;i<6;i++){fm1_diag_reset(&p);feed(&p,commands[i],0);CHECK(p.test_requested==i+1);}
        fm1_diag_reset(&p);feed(&p,"TEST AUDIO\nHELLO\n",0);CHECK(!p.test_requested);
        feed(&p,"TEST AUDIO\r\n",0);CHECK(!p.test_requested);
        feed(&p,"BEGIN 00000001 00000000\n",0);feed(&p,"TEST AUDIO\n",0);CHECK(!p.test_requested && !p.active);
        feed(&p,"TEST UNKNOWN\n",0);CHECK(!p.test_requested);
        feed(&p,"HELP\n",0);
#ifdef FM1_NES_PLAYER
        CHECK(strstr(response,"TEST NES"));
        fm1_diag_reset(&p);feed(&p,"TEST NES\n",0);CHECK(p.test_requested==7);
        fm1_diag_reset(&p);feed(&p,"TEST NES\nHELLO\n",0);CHECK(!p.test_requested);
        fm1_diag_reset(&p);feed(&p,"TEST NES\r\n",0);CHECK(!p.test_requested);
#else
        CHECK(strstr(response,"TEST KNOBS"));
        feed(&p,"TEST NES\n",0);CHECK(!p.test_requested);
#endif
    }
#endif
    fm1_diag_reset(&p);
    valid(&p);feed(&p,"COMMIT\n",31);CHECK(!p.verified && strstr(response,"COMMIT_BLOCKED"));
    feed(&p,"COMMIT\n",32);CHECK(strstr(response,"COMMIT_BLOCKED"));
    feed(&p,"BEGIN 00000000 00000000\n",0);CHECK(!p.active && strstr(response,"SIZE"));
    feed(&p,"BEGIN 00080001 00000000\n",0);CHECK(!p.active && strstr(response,"SIZE"));
    feed(&p,"BEGIN ffffffff 00000000\n",0);CHECK(!p.active);
    feed(&p,"BEGIN 00080000 00000000\n",0);CHECK(p.active && p.length==FM1_DIAG_LIMIT);
    feed(&p,"ABORT\n",0);CHECK(!p.active);
    feed(&p,"BEGIN 00000009 00000000\nDATA 00000000 313233343536373839\nEND\n",0);
    CHECK(!p.verified && strstr(response,"ERR"));
    feed(&p,"BEGIN 00000009 cbf43926\nEND\n",0);CHECK(!p.active && !p.verified);
    feed(&p,"BEGIN 00000009 cbf43926\nDATA 00000001 31\n",0);CHECK(!p.active);
    feed(&p,"BEGIN 00000001 00000000\nDATA 00000000 3132\n",0);CHECK(!p.active);
    feed(&p,"BEGIN 00000001 00000000\nDATA 00000000 zz\n",0);CHECK(!p.active);
    feed(&p,"BEGIN 00000002 00000000\nDATA 00000000 31\nDATA 00000000 31\n",0);CHECK(!p.active);
    feed(&p,"BEGIN 00000009 cbf43926\nBEGIN 00000009 cbf43926\n",0);CHECK(!p.active);
    memset(big,'X',sizeof(big));big[599]='\n';
    fm1_diag_feed(&p,(uint8_t *)big,sizeof(big),0,reply,NULL);CHECK(!p.active && strstr(response,"ERR LINE"));
    feed(&p,"HELLO\n",0);CHECK(strstr(response,"FM1DIAG/1"));
    fm1_diag_feed(&p,(const uint8_t *)"HEL\0LO\n",7,0,reply,NULL);CHECK(strstr(response,"ERR LINE"));
    feed(&p,"HELLO\r\n",0);CHECK(strstr(response,"ERR LINE"));
    fm1_diag_reset(&p);
    for(i=0;i<6;i++)fm1_diag_feed(&p,(const uint8_t *)"HELLO\n"+i,1,0,reply,NULL);
    CHECK(strstr(response,"FM1DIAG/1"));
    feed(&p,"BEGIN 00000009 cbf43926\n",0xfffffff0u);
    fm1_diag_tick(&p,0x00003000u,reply,NULL);CHECK(!p.active && strstr(response,"TIMEOUT"));
    feed(&p,"HEL",5);fm1_diag_tick(&p,10006,reply,NULL);CHECK(!p.used);
    valid(&p);fm1_diag_reset(&p);CHECK(!p.verified && !p.received);
    /* Packet length and direction validation over the entire EP0 length range. */
    for(i=0;i<=65535;i++) {
        CHECK(fm1_cdc_request_valid(0x21,0x20,0,0,(uint16_t)i)==(i==7));
        CHECK(fm1_cdc_request_valid(0xa1,0x21,0,0,(uint16_t)i)==(i<=7));
        CHECK(fm1_cdc_request_valid(0x21,0x22,3,0,(uint16_t)i)==(i==0));
    }
    for(t=0;t<256;t++)for(r=0;r<256;r++) {
        int expected=(t==0x21 && r==0x20)||(t==0xa1 && r==0x21);
        CHECK(fm1_cdc_request_valid((uint8_t)t,(uint8_t)r,0,0,7)==expected);
    }
    CHECK(!fm1_cdc_request_valid(0x21,0x20,0,1,7));
    CHECK(!fm1_cdc_request_valid(0x21,0x20,1,0,7));
    CHECK(!fm1_cdc_request_valid(0x21,0x22,4,0,0));
    /* Deterministic hostile-stream smoke test and buffer canaries. */
    for(i=0;i<100000;i++) {
        struct {uint32_t pre;fm1_diag_protocol p;uint32_t post;} g;
        uint8_t bytes[37];unsigned j;
        g.pre=0x12345678;g.post=0x87654321;fm1_diag_reset(&g.p);
        for(j=0;j<sizeof(bytes);j++){random=random*1664525u+1013904223u;bytes[j]=(uint8_t)(random>>24);}
        fm1_diag_feed(&g.p,bytes,sizeof(bytes),i,reply,NULL);
        CHECK(g.pre==0x12345678 && g.post==0x87654321 && g.p.used<FM1_DIAG_LINE);
    }
    printf("PASS protocol, malformed control requests, timeout/reset, commit rejection, 100000 hostile chunks (%u replies)\n",replies);
    return 0;
}
