#ifndef FM1_MOD_LIBRARY_H
#define FM1_MOD_LIBRARY_H

#include "usb.h"

#define FM1_MOD_LIBRARY_MAX 32u

typedef struct {
    char name[21];
    uint32_t size;
} fm1_mod_library_entry;

typedef struct {
    fm1_mod_library_entry entries[FM1_MOD_LIBRARY_MAX];
    uint16_t ticket;
    uint8_t count, selected, ready, pending, failed, saving;
    uint32_t last_ms;
} fm1_mod_library;

int fm1_mod_library_line(fm1_mod_library *, const char *, fm1_mod_reply, void *);
void fm1_mod_library_move(fm1_mod_library *, int);
int fm1_mod_library_pick(fm1_mod_library *);
int fm1_mod_library_save(fm1_mod_library *);
/* The USB owner refreshes last_ms on LIB POLL. Pending requests expire when
 * the PC stops servicing them; timeout never reports a song as saved. */
void fm1_mod_library_tick(fm1_mod_library *, uint32_t now);

#endif
