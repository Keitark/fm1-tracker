#ifndef FM1_MDX_USB_H
#define FM1_MDX_USB_H
#include <stddef.h>
#include <stdint.h>
#define FM1_MDX_UPLOAD_LIMIT (192u*1024u)
typedef struct {
    uint8_t bytes[FM1_MDX_UPLOAD_LIMIT];
    uint32_t length,received,crc,expected,last_ms,mdx_size,pdx_size;
    unsigned active,ready;
} fm1_mdx_upload;
enum { FM1_MDX_STOP=1,FM1_MDX_PLAY,FM1_MDX_DEMO,FM1_MDX_SELECT,FM1_MDX_MUTE,FM1_MDX_NOTE };
typedef void (*fm1_mdx_reply)(void *,const char *);
typedef struct {
    void *context;
    int (*idle)(void *);
    int (*request)(void *,unsigned,unsigned,unsigned);
    void (*status)(void *,char *,size_t);
} fm1_mdx_usb_io;
int fm1_mdx_usb_line(fm1_mdx_upload *,const fm1_mdx_usb_io *,const char *,uint32_t,fm1_mdx_reply,void *);
void fm1_mdx_usb_abort(fm1_mdx_upload *);
void fm1_mdx_usb_timeout(fm1_mdx_upload *,uint32_t);
/* Target wrappers used by the existing CDC parser. */
int fm1_mdx_usb_command(const char *,uint32_t,fm1_mdx_reply,void *);
void fm1_mdx_usb_reset(void);
void fm1_mdx_usb_tick(uint32_t);
#endif
