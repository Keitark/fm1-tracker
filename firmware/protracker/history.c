#include "history.h"
#include <string.h>

_Static_assert(sizeof(fm1_mod_history)<=11u*1024u,"history exceeds cache SRAM budget");
enum { CREATION_NONE, CREATION_BLANK, CREATION_CLONE };

static int valid_document(const fm1_mod_document *d){
    return d && d->loaded && d->orders>=1 && d->orders<=128 && d->patterns>=1 && d->patterns<=128 &&
        d->length<=FM1_MOD_CAPACITY && d->sample_offset==FM1_MOD_HEADER_BYTES+(size_t)d->patterns*1024u &&
        d->sample_offset<=d->length;
}
static int ready(const fm1_mod_history *h){
    return h && valid_document(h->document) && h->start<FM1_MOD_HISTORY_DEPTH &&
        h->count<=FM1_MOD_HISTORY_DEPTH && h->cursor<=h->count;
}
static int valid_editor(const fm1_mod_editor *e,const fm1_mod_document *d){
    return e && e->document==d && e->row<64 && e->channel<4 && e->field<FM1_FIELD_COUNT &&
        fm1_mod_get_order(d,e->order)>=0;
}
static void get_state(fm1_mod_history_state *state,const fm1_mod_document *d){
    state->length=(uint32_t)d->length;state->sample_offset=(uint32_t)d->sample_offset;
    state->orders=d->orders;state->patterns=d->patterns;
}
static int same_state(const fm1_mod_history_state *a,const fm1_mod_history_state *b){
    return a->length==b->length && a->sample_offset==b->sample_offset &&
        a->orders==b->orders && a->patterns==b->patterns;
}
static void set_state(fm1_mod_document *d,const fm1_mod_history_state *state){
    d->length=state->length;d->sample_offset=state->sample_offset;
    d->orders=state->orders;d->patterns=state->patterns;
}
static void get_cursor(fm1_mod_history_cursor *cursor,const fm1_mod_editor *e){
    cursor->order=e->order;cursor->row=e->row;cursor->channel=e->channel;cursor->field=e->field;
}
static void set_cursor(fm1_mod_editor *e,const fm1_mod_history_cursor *cursor){
    e->order=cursor->order;e->row=cursor->row;e->channel=cursor->channel;e->field=cursor->field;
}
static size_t pattern_offset(unsigned pattern){return FM1_MOD_HEADER_BYTES+(size_t)pattern*FM1_MOD_PATTERN_BYTES;}
static uint32_t pattern_crc(const fm1_mod_document *d,unsigned pattern){
    return fm1_mod_crc32(d->bytes+pattern_offset(pattern),FM1_MOD_PATTERN_BYTES);
}
static uint32_t scope_crc(const fm1_mod_document *d,unsigned scope,unsigned pattern,unsigned after){
    uint32_t crc=fm1_mod_crc32(d->bytes,FM1_MOD_HEADER_BYTES);
    if(scope==FM1_HISTORY_PATTERN || (scope==FM1_HISTORY_STRUCTURE && after)){
        uint32_t pat=pattern_crc(d,pattern);
        crc^=(pat<<1)|(pat>>31);
    }
    return crc;
}
static void set_dirty(fm1_mod_history *h){
    h->document->dirty=(uint8_t)(!h->saved_valid || h->current_id!=h->saved_id);
}
static void swap_bytes(uint8_t *a,uint8_t *b,unsigned length){
    unsigned i;for(i=0;i<length;i++){uint8_t value=a[i];a[i]=b[i];b[i]=value;}
}
void fm1_mod_history_init(fm1_mod_history *h,fm1_mod_document *d){
    if(!h)return;
    /* The large external record storage need not be cleared: count/active
     * prevent reading it until begin/commit have initialized each record. */
    h->document=d;h->start=h->count=h->cursor=h->active=0;
    h->current_id=h->saved_id=0;h->next_id=1;h->saved_valid=(uint8_t)(d && !d->dirty);
}
int fm1_mod_history_begin(fm1_mod_history *h,const fm1_mod_editor *e,unsigned scope){
    fm1_mod_history_record *r;int pattern;
    if(!ready(h) || h->active || scope>FM1_HISTORY_STRUCTURE || !valid_editor(e,h->document))return -1;
    /* Preserve unique state identifiers across undo branches. A theoretical
     * 32-bit wrap establishes a new barrier rather than reusing an old ID. */
    if(h->next_id==UINT32_MAX)fm1_mod_history_init(h,h->document);
    pattern=fm1_mod_get_order(h->document,e->order);r=&h->pending;
    r->scope=(uint8_t)scope;r->pattern=(uint8_t)(scope==FM1_HISTORY_STRUCTURE?h->document->patterns:(unsigned)pattern);
    r->source_pattern=(uint8_t)pattern;r->creation=CREATION_NONE;
    r->header_crc=fm1_mod_crc32(h->document->bytes,FM1_MOD_HEADER_BYTES);
    r->source_crc=pattern_crc(h->document,(unsigned)pattern);
    get_state(&r->before,h->document);get_cursor(&r->before_cursor,e);r->before_id=h->current_id;
    if(scope==FM1_HISTORY_PATTERN)memcpy(r->bytes,
        h->document->bytes+pattern_offset((unsigned)pattern),FM1_MOD_PATTERN_BYTES);
    else memcpy(r->bytes,h->document->bytes,FM1_MOD_HEADER_BYTES);
    r->before_crc=scope_crc(h->document,scope,r->pattern,0);h->active=1;return 0;
}
void fm1_mod_history_cancel(fm1_mod_history *h){if(h)h->active=0;}
static int unchanged_sample_sizes(const fm1_mod_history_record *r,const fm1_mod_document *d){
    unsigned slot;
    for(slot=0;slot<31;slot++)if(memcmp(r->bytes+20+slot*30+22,d->bytes+20+slot*30+22,2))return 0;
    return 1;
}
static int blank_pattern(const uint8_t *bytes){
    unsigned i;for(i=0;i<FM1_MOD_PATTERN_BYTES;i++)if(bytes[i])return 0;return 1;
}
int fm1_mod_history_commit(fm1_mod_history *h,const fm1_mod_editor *e){
    fm1_mod_history_record *r;fm1_mod_document *d;unsigned index,changed,shape;
    if(!h || !h->active)return -1;
    d=h->document;r=&h->pending;
    if(!ready(h) || !valid_editor(e,d)){fm1_mod_history_init(h,d);return -1;}
    get_state(&r->after,d);get_cursor(&r->after_cursor,e);
    changed=!same_state(&r->before,&r->after);
    if(r->scope==FM1_HISTORY_PATTERN){
        changed|=r->header_crc!=fm1_mod_crc32(d->bytes,FM1_MOD_HEADER_BYTES);
        changed|=memcmp(r->bytes,d->bytes+pattern_offset(r->pattern),FM1_MOD_PATTERN_BYTES)!=0;
    }else{
        changed|=memcmp(r->bytes,d->bytes,FM1_MOD_HEADER_BYTES)!=0;
        changed|=r->source_pattern>=d->patterns || r->source_crc!=pattern_crc(d,r->source_pattern);
    }
    if(!changed){h->active=0;return 0;}
    shape=r->scope==FM1_HISTORY_PATTERN?r->header_crc==fm1_mod_crc32(d->bytes,FM1_MOD_HEADER_BYTES):
        unchanged_sample_sizes(r,d) && r->source_pattern<d->patterns && r->source_crc==pattern_crc(d,r->source_pattern);
    if(r->scope==FM1_HISTORY_STRUCTURE){
        shape&=r->after.patterns==r->before.patterns+1 && r->before.patterns<128 &&
            r->after.sample_offset==r->before.sample_offset+FM1_MOD_PATTERN_BYTES &&
            r->after.length==r->before.length+FM1_MOD_PATTERN_BYTES;
    }else shape&=r->after.patterns==r->before.patterns && r->after.sample_offset==r->before.sample_offset &&
        r->after.length==r->before.length;
    if(!shape){fm1_mod_history_init(h,d);return -1;}
    if(r->scope==FM1_HISTORY_STRUCTURE){
        const uint8_t *created=d->bytes+pattern_offset(r->pattern);
        if(blank_pattern(created))r->creation=CREATION_BLANK;
        else if(!memcmp(created,d->bytes+pattern_offset(r->source_pattern),FM1_MOD_PATTERN_BYTES))r->creation=CREATION_CLONE;
        else {fm1_mod_history_init(h,d);return -1;}
    }
    r->after_crc=scope_crc(d,r->scope,r->pattern,1);r->after_id=h->next_id++;
    /* Only a completed change discards redo. Failed/no-op attempts retain it. */
    h->count=h->cursor;
    if(h->count==FM1_MOD_HISTORY_DEPTH){h->start=(uint8_t)((h->start+1)%FM1_MOD_HISTORY_DEPTH);h->count--;}
    index=(h->start+h->count)%FM1_MOD_HISTORY_DEPTH;memcpy(&h->records[index],r,sizeof(*r));
    h->count++;h->cursor=h->count;h->current_id=r->after_id;h->active=0;set_dirty(h);return 1;
}
static int travel(fm1_mod_history *h,fm1_mod_editor *e,unsigned redo){
    fm1_mod_history_record *r;fm1_mod_history_state current;fm1_mod_document *d;
    const fm1_mod_history_state *expected,*destination;unsigned index;
    if(!ready(h) || h->active || !e || e->document!=h->document ||
       (redo?h->cursor>=h->count:!h->cursor))return -1;
    index=(h->start+h->cursor-(redo?0u:1u))%FM1_MOD_HISTORY_DEPTH;
    r=&h->records[index];d=h->document;
    expected=redo?&r->before:&r->after;destination=redo?&r->after:&r->before;get_state(&current,d);
    if(!same_state(&current,expected) || scope_crc(d,r->scope,r->pattern,!redo)!=(redo?r->before_crc:r->after_crc)){
        fm1_mod_history_init(h,d);return -1;
    }
    /* Header-only changes and structural recipes never modify their source
     * pattern. Reject an out-of-band source edit before moving any PCM bytes. */
    if(r->scope!=FM1_HISTORY_PATTERN && (r->source_pattern>=d->patterns ||
       r->source_crc!=pattern_crc(d,r->source_pattern))){fm1_mod_history_init(h,d);return -1;}
    if(r->scope==FM1_HISTORY_STRUCTURE){
        if(redo){
            if(r->creation!=CREATION_BLANK && r->creation!=CREATION_CLONE){fm1_mod_history_init(h,d);return -1;}
            memmove(d->bytes+r->after.sample_offset,d->bytes+r->before.sample_offset,
                r->before.length-r->before.sample_offset);
            if(r->creation==CREATION_BLANK)memset(d->bytes+pattern_offset(r->pattern),0,FM1_MOD_PATTERN_BYTES);
            else memcpy(d->bytes+pattern_offset(r->pattern),d->bytes+pattern_offset(r->source_pattern),FM1_MOD_PATTERN_BYTES);
        }else{
            memmove(d->bytes+r->before.sample_offset,d->bytes+r->after.sample_offset,
                r->after.length-r->after.sample_offset);
        }
    }else if(r->scope==FM1_HISTORY_PATTERN)swap_bytes(r->bytes,
        d->bytes+pattern_offset(r->pattern),FM1_MOD_PATTERN_BYTES);
    if(r->scope!=FM1_HISTORY_PATTERN)swap_bytes(r->bytes,d->bytes,FM1_MOD_HEADER_BYTES);
    set_state(d,destination);
    set_cursor(e,redo?&r->after_cursor:&r->before_cursor);
    h->current_id=redo?r->after_id:r->before_id;
    if(redo)h->cursor++;else h->cursor--;set_dirty(h);return 0;
}
int fm1_mod_history_undo(fm1_mod_history *h,fm1_mod_editor *e){return travel(h,e,0);}
int fm1_mod_history_redo(fm1_mod_history *h,fm1_mod_editor *e){return travel(h,e,1);}
void fm1_mod_history_mark_saved(fm1_mod_history *h){
    if(!ready(h) || h->active)return;
    h->saved_id=h->current_id;h->saved_valid=1;h->document->dirty=0;
}
unsigned fm1_mod_history_undo_count(const fm1_mod_history *h){return ready(h)?h->cursor:0;}
unsigned fm1_mod_history_redo_count(const fm1_mod_history *h){return ready(h)?h->count-h->cursor:0;}
