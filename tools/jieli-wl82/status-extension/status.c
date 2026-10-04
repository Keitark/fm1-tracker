/* Experimental RAM-only FC03 status response, exact community WL82 helper.
 * No write-enable, array-program, erase, status-write, key or reset command.
 * Not uploaded automatically. Requires separate hardware qualification.
 */
typedef unsigned int u32;
typedef unsigned char u8;

#define SPI_CON 0x00011c00u
#define SPI_BUF 0x00011c08u
#define CS_OUT  0x000500c0u
#define CS_DIR  0x000500c8u
#define DEVICE  0x01c09708u
#define MODE    0x01c09750u
#define RX     0x1000u
#define CLEAR  0x4000u
#define DONE   0x8000u
#define POLLS  100000u

#ifdef FM1_STATUS_TEST
extern u32 status_read32(u32 address);
extern void status_write32(u32 address,u32 value);
#define READ(a) status_read32(a)
#define WRITE(a,v) status_write32(a,v)
#define SYNC() ((void)0)
#else
#define READ(a) (*(volatile u32 *)(a))
#define WRITE(a,v) (*(volatile u32 *)(a)=(v))
#define SYNC() __asm__ volatile("csync" ::: "memory")
#endif

static int wait_spi(void) {
    u32 count;
    SYNC();
    for(count=0;count<POLLS;++count) {
        if(READ(SPI_CON)&DONE) {
            WRITE(SPI_CON,READ(SPI_CON)|CLEAR);
            return 0;
        }
    }
    return -1;
}

#if defined(__clang__) || defined(__GNUC__)
__attribute__((used,noinline))
#endif
void fm1_loader_status(u8 *reply) {
    u32 i,control,cs;
    for(i=0;i<16;++i)reply[i]=0;
    reply[0]=0xfc;reply[1]=3;
    reply[2]=1; /* Rejected precondition, not a NOR status byte. */
    reply[4]='F';reply[5]='M';reply[6]='S';reply[7]='1';
    /* Only an initialized, ordinary SPI mode with inactive CS is supported.
       Never exit a continuous/quad mode or change the GPIO direction here. */
    if(!READ(DEVICE) || READ(MODE)!=0 || (READ(CS_DIR)&1u))return;
    cs=READ(CS_OUT);control=READ(SPI_CON);
    if(!(cs&1u) || !(control&1u))return;
    WRITE(SPI_CON,(control&~(RX|DONE))|CLEAR);
    WRITE(CS_OUT,cs&~1u);
    WRITE(SPI_BUF,0x05u); /* NOR read status register 1. */
    if(wait_spi())goto timeout;
    WRITE(SPI_CON,READ(SPI_CON)|RX);
    WRITE(SPI_BUF,0xffu); /* Clock one input byte, same RX mode as stock helper. */
    if(wait_spi())goto timeout;
    reply[3]=(u8)READ(SPI_BUF);
    reply[2]=0;
    goto restore;
timeout:
    reply[2]=2; /* Bounded SPI-controller timeout; never report ready. */
restore:
    WRITE(CS_OUT,cs);
    WRITE(SPI_CON,(control&~DONE)|CLEAR);
    SYNC();
}
