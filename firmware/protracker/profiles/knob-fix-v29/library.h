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
    uint8_t count, selected, ready, pending, failed;
} fm1_mod_library;

int fm1_mod_library_line(fm1_mod_library *, const char *, fm1_mod_reply, void *);
void fm1_mod_library_move(fm1_mod_library *, int);
int fm1_mod_library_pick(fm1_mod_library *);

#endif
