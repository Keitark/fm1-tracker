/* Host workshop bridge. Every action runs the production session/controller;
 * every displayed pixel comes from the production LCD renderer. No device I/O. */
#include "session.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif

static fm1_mod_document song;
static fm1_mod_history history;
static fm1_tracker_session session;
static fm1_mod_library library;
static fm1_bank bank;
static fm1_tracker_view view;
static uint8_t bank_bytes[128u*1024u];

API int fm1_sim_init(const uint8_t *mod,unsigned length,const uint8_t *pcm,unsigned size){
    if(!pcm || size>sizeof(bank_bytes))return -1;
    memcpy(bank_bytes,pcm,size);
    if(fm1_bank_load(&bank,bank_bytes,size))return -1;
    if(mod && length){
        if(length>FM1_MOD_CAPACITY)return -1;
        memcpy(song.bytes,mod,length);if(fm1_mod_open(&song,length))return -1;
    }else fm1_mod_new(&song);
    memset(&library,0,sizeof(library));
    fm1_tracker_session_init(&session,&song,&library,&bank,&history,0,0);return 0;
}
API void fm1_sim_key(unsigned slot,unsigned down,uint32_t now){
    fm1_tracker_session_key(&session,slot,down,now);
}
API void fm1_sim_encoder(unsigned id,int step,uint32_t now){
    fm1_tracker_session_encoder(&session,id,step,now);
}
API unsigned fm1_sim_audio(int16_t *pcm,unsigned frames,uint32_t now){
    if(!pcm || frames>44100u)return 0;
    fm1_tracker_session_tick(&session,now);
    if(session.running || session.player.preview_active)fm1_mod_render(&session.player,pcm,frames);
    else memset(pcm,0,(size_t)frames*4u);
    return frames;
}
API int fm1_sim_frame(uint8_t *rgb,unsigned size,uint32_t now){
    uint8_t rows[3u*480u];unsigned y,x;
    if(!rgb || size<240u*240u*3u)return -1;
    fm1_tracker_session_tick(&session,now);
    fm1_tracker_view_update(&view,&song,&session.player,&session.editor,&library,
        session.page,session.running,session.stereo,0);
    fm1_tracker_view_context_apply(&view,&song,&session.editor,&session.view_context);
    for(y=0;y<240;y+=3){
        fm1_tracker_view_render(&view,y,3,rows);
        for(x=0;x<720;x++){
            unsigned pixel=((unsigned)rows[x*2]<<8)|rows[x*2+1],index=(y*240u+x)*3u;
            rgb[index]=(uint8_t)(((pixel>>12)&15u)*17u);
            rgb[index+1]=(uint8_t)(((pixel>>7)&15u)*17u);
            rgb[index+2]=(uint8_t)(((pixel>>1)&15u)*17u);
        }
    }
    return 0;
}
API int fm1_sim_state(char *out,unsigned size){
    return snprintf(out,size,
        "{\"running\":%u,\"dirty\":%u,\"write\":%u,\"order\":%u,\"row\":%u,"
        "\"channel\":%u,\"sample\":%u,\"field\":%u,\"step\":%u,\"page\":%u,"
        "\"patterns\":%u,\"orders\":%u,\"bytes\":%u,\"undo\":%u,\"redo\":%u,\"stereo\":%u}",
        session.running,song.dirty,session.editor.edit,
        session.running?session.player.current_order:session.editor.order,
        session.running?session.player.current_row:session.editor.row,session.editor.channel,
        session.editor.sample,session.editor.field,session.editor.step,session.page,
        song.patterns,song.orders,(unsigned)song.length,fm1_mod_history_undo_count(&history),
        fm1_mod_history_redo_count(&history),session.stereo);
}
API unsigned fm1_sim_export(uint8_t *out,unsigned capacity){
    if(!song.loaded || !out || capacity<song.length)return 0;
    memcpy(out,song.bytes,song.length);return (unsigned)song.length;
}
