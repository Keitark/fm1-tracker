#include "rx_channel.h"
#include <string.h>
void fm1_rx_reset(fm1_rx_channel *c, unsigned generation) {
    memset(c,0,sizeof(*c)); c->generation=generation;
}
static void fail(fm1_rx_channel *c) {
    c->armed=c->matched=0; c->read=c->write=0; c->fault=1;
}
void fm1_rx_sync(fm1_rx_channel *c, unsigned generation, int ready, uint32_t now) {
    if(c->generation!=generation || !ready)fm1_rx_reset(c,generation);
    if(c->armed && (uint32_t)(now-c->armed_ms)>=FM1_BOOT_TIMEOUT_MS)fail(c);
}
unsigned fm1_rx_take(fm1_rx_channel *c, uint8_t *out, unsigned limit) {
    unsigned i,n=c->write-c->read;
    if(c->armed || c->fault)return 0;
    if(n>limit)n=limit;
    for(i=0;i<n;i++)out[i]=c->bytes[(c->read++)%FM1_RX_CAPACITY];
    return n;
}
int fm1_rx_arm(fm1_rx_channel *c, uint32_t now) {
    if(c->fault || c->armed || c->read!=c->write)return 0;
    c->armed=1; c->matched=0; c->armed_ms=now; return 1;
}
int fm1_rx_receive(fm1_rx_channel *c, const uint8_t *data, unsigned n, uint32_t now) {
    static const char confirm[]=FM1_BOOT_CONFIRM;
    unsigned i;
    if(c->fault)return 0;
    if(c->armed) {
        if((uint32_t)(now-c->armed_ms)>=FM1_BOOT_TIMEOUT_MS ||
           n>sizeof(confirm)-1-c->matched) {fail(c);return 0;}
        for(i=0;i<n;i++) {
            if(data[i]!=(uint8_t)confirm[c->matched++]) {fail(c);return 0;}
        }
        if(c->matched==sizeof(confirm)-1) {c->armed=0;return 1;}
        return 0;
    }
    if(n>FM1_RX_CAPACITY-(c->write-c->read)) {fail(c);return 0;}
    for(i=0;i<n;i++)c->bytes[(c->write++)%FM1_RX_CAPACITY]=data[i];
    return 0;
}
