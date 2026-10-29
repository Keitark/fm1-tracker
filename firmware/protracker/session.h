#ifndef FM1_TRACKER_SESSION_H
#define FM1_TRACKER_SESSION_H
#include "bank.h"
#include "controls.h"
#include "history.h"
#include "view.h"

/* Task-owned controller shared by firmware and host scenarios. It performs no
 * SDK calls, allocation, device I/O or waits. The caller serializes document
 * replacement and calls with interrupts enabled so PCM moves remain bounded. */
typedef struct {
    fm1_mod_document *song;
    fm1_mod_library *library;
    const fm1_bank *bank;
    fm1_mod_history *history;
    fm1_mod_player player;
    fm1_mod_editor editor;
    fm1_tracker_controls gestures;
    fm1_tracker_view_context view_context;
    void (*flush)(void *);
    void *flush_context;
    uint32_t notice_until;
    uint16_t confirm_bank_index, library_ticket;
    uint8_t confirm_sample, page, stereo, running, busy, mute_mask, solo_mask;
    uint8_t library_pending, library_saving;
} fm1_tracker_session;

void fm1_tracker_session_init(fm1_tracker_session *, fm1_mod_document *,
    fm1_mod_library *, const fm1_bank *, fm1_mod_history *,
    void (*flush)(void *), void *flush_context);
void fm1_tracker_session_reset_after_load(fm1_tracker_session *, uint32_t now);
void fm1_tracker_session_key(fm1_tracker_session *, unsigned slot,
    unsigned down, uint32_t now);
void fm1_tracker_session_encoder(fm1_tracker_session *, unsigned id,
    int detents, uint32_t now);
void fm1_tracker_session_tick(fm1_tracker_session *, uint32_t now);
void fm1_tracker_session_stop(fm1_tracker_session *);
int fm1_tracker_session_play(fm1_tracker_session *, unsigned pattern,
    unsigned from_cursor, uint32_t now);
/* Uses FM1_MOD_REQUEST_* values. NEW arms confirmation when the song is dirty. */
int fm1_tracker_session_action(fm1_tracker_session *, unsigned action, uint32_t now);
void fm1_tracker_session_notice(fm1_tracker_session *, const char *, uint32_t now);
#endif
