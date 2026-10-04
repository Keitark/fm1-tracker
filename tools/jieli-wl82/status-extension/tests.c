/* A register model, not silicon emulation or an upload qualification. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned int u32;
typedef unsigned char u8;
void fm1_loader_status(u8 *reply);
static u32 device,mode,dir,out,control,buffer;
static u32 writes,commands,polls,max_polls,timeout_phase,returned_status;
#define CHECK(c) do {if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
#define CON 0x11c00u
#define BUF 0x11c08u
#define OUT 0x500c0u
#define DIR 0x500c8u

u32 status_read32(u32 address) {
    switch(address) {
    case 0x1c09708u:return device;
    case 0x1c09750u:return mode;
    case DIR:return dir;
    case OUT:return out;
    case CON:
        if(commands && !(out&1)) {
            ++polls;
            if(polls>max_polls)max_polls=polls;
            if(commands!=timeout_phase && polls>=2)control|=0x8000;
        }
        return control;
    case BUF:return commands==2?returned_status:buffer;
    default:CHECK(0);return 0;
    }
}
void status_write32(u32 address,u32 value) {
    ++writes;
    switch(address) {
    case CON:
        control=value&~0xc000u; /* W1C done acknowledgment; flags not config. */
        return;
    case OUT:out=value;return;
    case BUF:
        CHECK(!(out&1));
        ++commands;polls=0;control&=~0x8000u;
        if(commands==1)CHECK(value==5 && !(control&0x1000));
        else CHECK(commands==2 && value==255 && (control&0x1000));
        buffer=value;return;
    default:CHECK(0);
    }
}
static void init(void) {
    device=0x1c099bc;mode=0;dir=0x10;out=0xa5;control=0x9011;
    buffer=0;writes=commands=polls=max_polls=timeout_phase=0;
    returned_status=0;
}
static void run_reply(u8 *reply) {
    memset(reply,0xaa,16);fm1_loader_status(reply);
    CHECK(reply[0]==0xfc && reply[1]==3);
    CHECK(memcmp(reply+4,"FMS1",4)==0);
    CHECK(memcmp(reply+8,"\0\0\0\0\0\0\0\0",8)==0);
}
int main(void) {
    unsigned i;u8 reply[16];
    for(i=0;i<256;++i) { /* Busy/WEL/protection bits all pass through unchanged. */
        init();returned_status=i;run_reply(reply);
        CHECK(reply[2]==0 && reply[3]==i && commands==2);
        CHECK(out==0xa5 && dir==0x10 && control==0x1011);
        CHECK(max_polls<100000);
    }
    for(i=1;i<=2;++i) {
        init();timeout_phase=i;run_reply(reply);
        CHECK(reply[2]==2 && reply[3]==0 && commands==i);
        CHECK(max_polls==100000);
        CHECK(out==0xa5 && dir==0x10 && control==0x1011);
    }
    for(i=0;i<5;++i) {
        init();
        switch(i) {
        case 0:device=0;break;
        case 1:mode=1;break;
        case 2:dir|=1;break;
        case 3:out&=~1u;break;
        case 4:control&=~1u;break;
        }
        run_reply(reply);CHECK(reply[2]==1 && writes==0 && commands==0);
    }
    puts("256 status values, both transfer timeouts, and five preconditions passed");
    return 0;
}
