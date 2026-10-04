#include "rx_channel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
static int send(fm1_rx_channel *c,const char *s,uint32_t now) {
    return fm1_rx_receive(c,(const uint8_t *)s,(unsigned)strlen(s),now);
}
int main(void) {
    fm1_rx_channel c;uint8_t out[FM1_RX_CAPACITY+1];unsigned split,i;
    const char *confirm=FM1_BOOT_CONFIRM;
    fm1_rx_reset(&c,1);
    CHECK(!send(&c,confirm,0)); /* Never transition without arming. */
    CHECK(!fm1_rx_arm(&c,0));CHECK(fm1_rx_take(&c,out,sizeof(out))==strlen(confirm));
    for(split=0;split<=strlen(confirm);split++) {
        fm1_rx_reset(&c,1);CHECK(fm1_rx_arm(&c,0));
        CHECK(fm1_rx_receive(&c,(const uint8_t *)confirm,split,1)==(split==strlen(confirm)));
        if(split<strlen(confirm))CHECK(send(&c,confirm+split,2));
        CHECK(!c.armed);CHECK(!send(&c,confirm,3)); /* one-shot */
    }
    fm1_rx_reset(&c,1);CHECK(fm1_rx_arm(&c,0));
    for(i=0;i<strlen(confirm);i++)CHECK(fm1_rx_receive(&c,(const uint8_t *)confirm+i,1,1)==(i+1==strlen(confirm)));
    for(i=0;i<strlen(confirm);i++) {
        char bad[32];strcpy(bad,confirm);bad[i]='X';
        fm1_rx_reset(&c,1);CHECK(fm1_rx_arm(&c,0));CHECK(!send(&c,bad,1));CHECK(c.fault);
        CHECK(!send(&c,confirm,2));CHECK(!fm1_rx_arm(&c,3));
    }
    fm1_rx_reset(&c,1);CHECK(fm1_rx_arm(&c,0));CHECK(!send(&c,"UBOOT CONFIRM\nX",1));CHECK(c.fault);
    fm1_rx_reset(&c,1);CHECK(fm1_rx_arm(&c,0));CHECK(!send(&c,"UBOOT CONFIRM\r\n",1));CHECK(c.fault);
    fm1_rx_reset(&c,1);CHECK(fm1_rx_arm(&c,0));CHECK(!send(&c,confirm,5000));CHECK(c.fault);
    fm1_rx_reset(&c,1);CHECK(fm1_rx_arm(&c,0xfffffff0u));CHECK(send(&c,confirm,0x20));
    fm1_rx_reset(&c,1);CHECK(fm1_rx_arm(&c,0xfffffff0u));fm1_rx_sync(&c,1,1,0x2000);CHECK(c.fault);
    fm1_rx_reset(&c,1);CHECK(fm1_rx_arm(&c,0));fm1_rx_sync(&c,2,1,1);CHECK(!send(&c,confirm,2));
    fm1_rx_reset(&c,1);CHECK(fm1_rx_arm(&c,0));fm1_rx_sync(&c,1,0,1);CHECK(!send(&c,confirm,2));
    fm1_rx_reset(&c,1);memset(out,'A',sizeof(out));
    CHECK(!fm1_rx_receive(&c,out,sizeof(out),0));CHECK(c.fault);CHECK(!fm1_rx_take(&c,out,sizeof(out)));
    fm1_rx_reset(&c,1);c.read=c.write=UINT_MAX-20;
    CHECK(!fm1_rx_receive(&c,out,FM1_RX_CAPACITY,0));CHECK(!fm1_rx_arm(&c,0));
    CHECK(fm1_rx_take(&c,out,FM1_RX_CAPACITY)==FM1_RX_CAPACITY);CHECK(fm1_rx_arm(&c,1));
    CHECK(!fm1_rx_take(&c,out,64));
    puts("PASS RX ring, overflow, exact boot confirmation, fragmentation, timeout/wrap, generation/DTR invalidation");
    return 0;
}
