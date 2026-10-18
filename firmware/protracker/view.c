#include "view.h"
#include <stdio.h>
#include <string.h>

enum { INK=0x084b, PANEL=0x7c72, PANEL_DARK=0x4aef, EDGE=0xad77,
       TEXT=0xe73c, FAINT=0x8c75, CYAN=0x6fff, GREEN=0x67e8,
       CURSOR=0xc638, BLACK=0x0000, AMBER=0xffcc, RED=0xf9a6,
       BLOCK=0x3259 };

/* Original fonts expanded to ROM scanlines: no 64-bit shifts in the LCD loop. */
static const uint8_t small_font[96][5]={
    [3]={2,7,5,7,2}, /* # */
    [10]={1,2,7,2,0}, /* * */
    [11]={0,2,7,2,0}, /* + */
    [13]={0,0,7,0,0}, /* - */
    [14]={0,0,0,0,2}, /* . */
    [15]={1,1,1,2,0}, /* / */
    [16]={7,5,5,5,7}, /* 0 */
    [17]={2,2,2,2,2}, /* 1 */
    [18]={7,1,7,4,7}, /* 2 */
    [19]={7,1,7,1,7}, /* 3 */
    [20]={5,5,7,1,1}, /* 4 */
    [21]={7,4,7,1,7}, /* 5 */
    [22]={7,4,7,5,7}, /* 6 */
    [23]={7,1,1,1,1}, /* 7 */
    [24]={7,5,7,5,7}, /* 8 */
    [25]={7,5,7,1,7}, /* 9 */
    [26]={0,0,2,2,0}, /* : */
    [30]={4,2,1,2,4}, /* > */
    [33]={2,5,7,5,5}, /* A */
    [34]={6,5,6,5,6}, /* B */
    [35]={7,4,3,4,7}, /* C */
    [36]={6,5,5,5,6}, /* D */
    [37]={7,4,7,4,7}, /* E */
    [38]={7,4,7,4,4}, /* F */
    [39]={7,4,3,5,7}, /* G */
    [40]={5,5,7,5,5}, /* H */
    [41]={7,2,2,2,7}, /* I */
    [42]={1,1,1,1,7}, /* J */
    [43]={5,5,6,5,5}, /* K */
    [44]={4,4,4,4,7}, /* L */
    [45]={7,7,7,5,5}, /* M */
    [46]={7,5,5,5,5}, /* N */
    [47]={7,5,5,5,7}, /* O */
    [48]={7,5,7,4,4}, /* P */
    [49]={7,5,5,7,7}, /* Q */
    [50]={7,5,6,5,5}, /* R */
    [51]={7,4,7,1,7}, /* S */
    [52]={7,2,2,2,2}, /* T */
    [53]={5,5,5,5,7}, /* U */
    [54]={5,5,5,5,2}, /* V */
    [55]={5,5,5,7,7}, /* W */
    [56]={5,5,2,5,5}, /* X */
    [57]={5,5,2,2,2}, /* Y */
    [58]={7,1,7,4,7}, /* Z */
};
static const uint8_t grid_font[96][7]={
    [3]={10,10,31,10,31,10,10}, /* # */
    [13]={0,0,0,31,0,0,0}, /* - */
    [16]={14,17,19,21,25,17,14}, /* 0 */
    [17]={4,12,4,4,4,4,14}, /* 1 */
    [18]={14,17,1,2,4,8,31}, /* 2 */
    [19]={30,1,1,14,1,1,30}, /* 3 */
    [20]={2,6,10,18,31,2,2}, /* 4 */
    [21]={31,16,16,30,1,1,30}, /* 5 */
    [22]={14,16,16,30,17,17,14}, /* 6 */
    [23]={31,1,2,4,8,8,8}, /* 7 */
    [24]={14,17,17,14,17,17,14}, /* 8 */
    [25]={14,17,17,15,1,1,14}, /* 9 */
    [30]={16,8,4,2,4,8,16}, /* > */
    [33]={14,17,17,31,17,17,17}, /* A */
    [34]={30,17,17,30,17,17,30}, /* B */
    [35]={15,16,16,16,16,16,15}, /* C */
    [36]={30,17,17,17,17,17,30}, /* D */
    [37]={31,16,16,30,16,16,31}, /* E */
    [38]={31,16,16,30,16,16,16}, /* F */
    [39]={15,16,16,23,17,17,15}, /* G */
    [40]={17,17,17,31,17,17,17}, /* H */
    [47]={14,17,17,17,17,17,14}, /* O */
    [50]={30,17,17,30,20,18,17}, /* R */
    [55]={17,17,17,21,21,21,10}, /* W */
};

static void display_name(char *out,const uint8_t *raw,unsigned n,unsigned limit){
    unsigned i;
    for(i=0;i<n && i<limit;i++){
        unsigned c=raw[i];if(!c)break;
        if(c>='a' && c<='z')c-=32;
        out[i]=(c>='A' && c<='Z') || (c>='0' && c<='9') ||
            c==' ' || c=='-' || c=='.'?(char)c:' ';
    }
    while(i && out[i-1]==' ')i--;
    out[i]=0;
}

void fm1_tracker_view_update(fm1_tracker_view *v,const fm1_mod_document *d,
    const fm1_mod_player *p,const fm1_mod_editor *e,const fm1_mod_library *lib,
    unsigned page,unsigned running,unsigned stereo,unsigned save_hint){
    unsigned i,ch,order=running?p->current_order:e->order;
    unsigned row=running?p->current_row:e->row;
    unsigned pattern=d->loaded?d->bytes[952+order]:0;
    char name[24];
    memset(v->ui,0,sizeof(v->ui));memset(v->grid,0,sizeof(v->grid));
    v->page=(uint8_t)page;v->loaded=d->loaded;v->channel=e->channel;
    v->write=e->edit;v->sample_mode=e->sample_mode;
    v->field=e->field;memset(v->block_mask,0,sizeof(v->block_mask));
    for(ch=0;ch<4;ch++){
        v->peak[ch]=0;
        for(i=0;i<48;i++){
            int sample=(running || p->preview_active)?p->scope[ch][(p->scope_head+i)%48]:0;
            unsigned level=(unsigned)(sample<0?-sample:sample);if(level>64)level=64;
            v->scope[ch][i]=(int8_t)sample;
            if(level>v->peak[ch])v->peak[ch]=(uint8_t)level;
        }
    }
    if(page==1){
        unsigned first=lib->selected>3?lib->selected-3:0;
        snprintf(v->ui[0],40,"FILES FROM USB PC");
        snprintf(v->ui[1],40,"%02u FILES %s",lib->count,lib->pending?"LOADING":
            lib->failed?"ERROR":lib->ready?"READY":"CONNECT");
        for(i=0;i<7;i++){
            unsigned index=first+i;
            if(index<lib->count)snprintf(v->ui[i+2],40,"%c%02u %.16s",
                index==lib->selected?'>':' ',index+1,lib->entries[index].name);
        }
        snprintf(v->ui[9],40,"PLAY LOAD HOME BACK");return;
    }
    if(page==2){
        unsigned first=e->sample>3?e->sample-3:1;if(first>25)first=25;
        snprintf(v->ui[0],40,"SAMPLES IN MOD");
        snprintf(v->ui[1],40,"PRESETS SELECT S%02X",e->sample);
        for(i=0;i<7;i++){
            unsigned index=first+i,h=20u+(index-1u)*30u;
            if(index<=31 && d->loaded){
                display_name(name,d->bytes+h,22,16);
                snprintf(v->ui[i+2],40,"%c%02X %.16s",
                    index==e->sample?'>':' ',index,name);
            }
        }
        snprintf(v->ui[9],40,"EDIT OR HOME BACK");return;
    }
    if(!d->loaded){
        snprintf(v->ui[0],40,"FM1 PROTRACKER");
        snprintf(v->ui[1],40,"USB LOAD OR MOD NEW");return;
    }
    display_name(name,d->bytes,20,20);
    snprintf(v->ui[0],40,"%-23.20s %s%c",name,stereo?"ST ":"SPK",d->dirty?'*':' ');
    snprintf(v->ui[1],40,"%s O%02u P%02u S%02X T%02u OC%+d",
        e->edit?"WRITE":running?(p->playing==2?"PAT":"PLAY"):"EDIT",
        order,pattern,e->sample,e->step,e->octave);
    {
        unsigned h=20u+(e->sample-1u)*30u;
        int fine=d->bytes[h+24];if(fine>=8)fine-=16;
        display_name(name,d->bytes+h,22,12);
        static const char *const fields[]={"NOTE","SMP","FX","PARAM"};
        snprintf(v->ui[2],40,"%-5s %-12.12s V%02u F%+d",
            fields[e->field<FM1_FIELD_COUNT?e->field:0],
            name,d->bytes[h+25],fine);
        if(save_hint)snprintf(v->ui[2],40,"USB SAVE TO COMPUTER");
    }
    memcpy(v->grid[0],"ROW CH1      CH2      CH3      CH4     ",40);
    /* The highlighted row never moves. Blank margins keep the pattern boundary
     * visible instead of falsely showing row 00 underneath row 63. */
    for(i=0;i<9;i++){
        int shown=(int)row+(int)i-4;unsigned pos,k;
        if(shown<0 || shown>=64)continue;
        if(e->block_active && e->block_order==order){
            unsigned low=e->block_anchor_row<e->block_end_row?e->block_anchor_row:e->block_end_row;
            unsigned high=e->block_anchor_row>e->block_end_row?e->block_anchor_row:e->block_end_row;
            unsigned left=e->block_anchor_channel<e->block_end_channel?e->block_anchor_channel:e->block_end_channel;
            unsigned right=e->block_anchor_channel>e->block_end_channel?e->block_anchor_channel:e->block_end_channel;
            if((unsigned)shown>=low && (unsigned)shown<=high)
                for(ch=left;ch<=right;ch++)v->block_mask[i]|=(uint8_t)(1u<<ch);
        }
        pos=(unsigned)snprintf(v->grid[i+1],40,"%c%02u ",i==4?'>':' ',(unsigned)shown);
        for(ch=0;ch<4;ch++){
            fm1_mod_cell cell={0,0,0,0};char formatted[12];
            static const uint8_t compact_index[8]={0,1,2,4,5,7,8,9};
            fm1_mod_cell_get(d,pattern,(unsigned)shown,ch,&cell);
            fm1_mod_format_cell(cell,formatted);
            for(k=0;k<8;k++)v->grid[i+1][pos++]=formatted[compact_index[k]];
            if(ch<3)v->grid[i+1][pos++]=' ';
        }
        v->grid[i+1][pos]=0;
    }
}

void fm1_tracker_view_context_apply(fm1_tracker_view *v,const fm1_mod_document *d,
    const fm1_mod_editor *e,const fm1_tracker_view_context *context){
    unsigned i,start,pos=0;
    if(!context || v->page || !d->loaded)return;
    if(context->arranger){
        snprintf(v->ui[0],40,"ORDER %02u/%02u PATTERN %02u",e->order+1,d->orders,
                 (unsigned)d->bytes[952+e->order]);
        memset(v->ui[1],0,40);start=e->order>1?e->order-1:0;
        for(i=start;i<d->orders && i<start+4;i++){
            int count=snprintf(v->ui[1]+pos,40-pos,"%c%02u:%02u ",
                i==e->order?'>':' ',i+1,(unsigned)d->bytes[952+i]);
            if(count<0 || (unsigned)count>=40-pos)break;pos+=(unsigned)count;
        }
        snprintf(v->ui[2],40,"FX ADD ENV INS SCL DEL HOME");
    }else if(context->bank_open){
        snprintf(v->ui[0],40,"BANK %02u/%02u -> SONG S%02X",
            context->bank_count?context->bank_index+1:0,context->bank_count,e->sample);
        snprintf(v->ui[1],40,"%.23s",context->bank_name);
        snprintf(v->ui[2],40,"KN1 HEAR FX COPY ENV BACK");
    }else if(context->new_hold)snprintf(v->ui[2],40,"HOLD SEQ+REC: NEW EMPTY SONG");
    else if(context->quick_modifier)snprintf(v->ui[2],40,"Q1-8 SELECT HOLD600 BIND");
    else if(context->block_modifier)snprintf(v->ui[2],40,"SEQ+SCL COPY SEQ+LFO PASTE");
    if(context->notice[0])snprintf(v->ui[2],40,"%.39s",context->notice);
}

static uint8_t font_row(const uint8_t *font,unsigned height,char c,unsigned row){
    unsigned code=(unsigned char)c;
    if(code>='a' && code<='z')code-=32;
    return code>=32 && code<128 && row<height?font[(code-32)*height+row]:0;
}
static void pixel(uint8_t **out,uint16_t color){
    *(*out)++=(uint8_t)(color>>8);*(*out)++=(uint8_t)color;
}
static void small_line(uint8_t *out,const char *text,unsigned local,
    unsigned scale,unsigned width,uint16_t background,uint16_t foreground){
    unsigned col,x,gy=local>=3?(local-3)/scale:5;
    size_t length=strlen(text);
    for(col=0;col<240/width;col++){
        uint8_t bits=col<length?font_row(&small_font[0][0],5,text[col],gy):0;
        for(x=0;x<width;x++){
            unsigned gx=x/scale;
            pixel(&out,gx<3 && (bits&(1u<<(2-gx)))?foreground:background);
        }
    }
}

void fm1_tracker_view_render(const fm1_tracker_view *v,unsigned y,unsigned rows,uint8_t *out){
    unsigned dy;
    if(y>=240)return;if(rows>240-y)rows=240-y;
    for(dy=0;dy<rows;dy++){
        unsigned physical=y+dy,x,ch;uint8_t *line=out+dy*480;
        if(v->page || !v->loaded){
            unsigned index=physical/24,local=physical%24;
            uint16_t background=index<2?PANEL_DARK:INK;
            uint16_t fg=index==1?AMBER:TEXT;
            if(v->ui[index][0]=='>'){background=CURSOR;fg=BLACK;}
            small_line(line,v->ui[index],local,3,12,background,fg);continue;
        }
        if(physical<48){
            unsigned index=physical/16,local=physical%16;
            small_line(line,v->ui[index],local,2,8,index==1?PANEL_DARK:PANEL,
                index==1 && v->write?AMBER:TEXT);
            if(local==15){uint8_t *q=line;for(x=0;x<240;x++)pixel(&q,EDGE);}
            continue;
        }
        if(physical<80){
            unsigned local_y=physical-48;
            uint8_t *q=line;
            for(ch=0;ch<4;ch++){
                unsigned peak=v->peak[ch];uint16_t trace_color=ch==v->channel?AMBER:GREEN;
                for(x=0;x<60;x++){
                    uint16_t color=(x==0 || x==59 || local_y==0 || local_y==31)?EDGE:INK;
                    if(local_y==16 && x>2 && x<54)color=PANEL_DARK;
                    if(x>=4 && x<52){
                        unsigned idx=x-4;int a=16-v->scope[ch][idx]*12/64;
                        int b=16-v->scope[ch][idx?idx-1:idx]*12/64;
                        int lo=a<b?a:b,hi=a>b?a:b;
                        if(local_y>2 && local_y<30 && (int)local_y>=lo && (int)local_y<=hi)
                            color=trace_color;
                    }
                    if(x>=55 && x<=57 && local_y>3 && local_y<29 &&
                       local_y>=29-peak*25/64)color=GREEN;
                    pixel(&q,color);
                }
            }
            continue;
        }
        {
            unsigned index=(physical-80)/16,local=(physical-80)%16;
            unsigned active=index==5,col;
            uint8_t *q=line;size_t length=strlen(v->grid[index]);
            /* Scaled glyphs repeat in pairs. Reuse the row already produced
             * in this strip, leaving the shared 3-row LCD staging size intact. */
            if(dy && local>1 && local<15 && !(local&1)){
                memcpy(line,line-480,480);continue;
            }
            for(col=0;col<40;col++){
                unsigned selected=col>=4+v->channel*9 && col<12+v->channel*9;
                static const uint8_t field_begin[4]={0,3,5,6},field_end[4]={3,5,6,8};
                unsigned base=4+v->channel*9,field_id=v->field<4?v->field:0;
                unsigned field=selected && col>=base+field_begin[field_id] && col<base+field_end[field_id];
                unsigned block_ch=col>=4?(col-4)/9:4;
                unsigned block=index>0 && block_ch<4 && (col-4)%9<8 &&
                    (v->block_mask[index-1]&(1u<<block_ch));
                uint16_t bg=index==0?PANEL:active?CURSOR:INK;
                uint16_t fg=index==0?TEXT:active?BLACK:TEXT;
                uint8_t bits=col<length && local>0 && local<15?
                    font_row(&grid_font[0][0],7,v->grid[index][col],(local-1)/2):0;
                if(index==0 && selected){bg=PANEL_DARK;fg=AMBER;}
                if(block && !active)bg=BLOCK;
                if(active && selected)bg=CYAN;
                if(active && field)bg=v->write?RED:AMBER;
                if(!active && index>0 && col<4)fg=FAINT;
                if(!active && index>0 && selected)fg=CYAN;
                for(x=0;x<6;x++){
                    uint16_t color=x<5 && (bits&(1u<<(4-x)))?fg:bg;
                    if(active && (local==0 || local==15))color=EDGE;
                    if(index==0 && local==15)color=selected?AMBER:EDGE;
                    if(col==39)color=EDGE;
                    pixel(&q,color);
                }
            }
        }
    }
}
