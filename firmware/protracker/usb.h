#ifndef FM1_MOD_USB_H
#define FM1_MOD_USB_H
#include "mod.h"
typedef void (*fm1_mod_reply)(void *, const char *);
typedef struct {
    size_t length, received, export_length;
    uint32_t expected_crc, crc, export_crc, last_ms;
    unsigned active, exporting;
} fm1_mod_transfer;
typedef struct {
    void *context;
    int (*idle)(void *);
    int (*request)(void *, unsigned);
    void (*status)(void *, char *, size_t);
} fm1_mod_usb_io;
enum { FM1_MOD_REQUEST_STOP=1, FM1_MOD_REQUEST_PLAY, FM1_MOD_REQUEST_NEW,
       FM1_MOD_REQUEST_PATTERN };
int fm1_mod_usb_line(fm1_mod_document *, fm1_mod_transfer *, const fm1_mod_usb_io *,
                    const char *, uint32_t, fm1_mod_reply, void *);
void fm1_mod_usb_abort(fm1_mod_transfer *);
void fm1_mod_usb_timeout(fm1_mod_transfer *, uint32_t);
int fm1_mod_usb_command(const char *, uint32_t, fm1_mod_reply, void *);
void fm1_mod_usb_reset(void);
void fm1_mod_usb_tick(uint32_t);
#endif
