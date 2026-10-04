/* Offline screenshots use the same renderer and RGB444 truncation as the FM-1. */
#include "view.h"
#include "bank.h"
#include <stdio.h>
#include <string.h>

static fm1_mod_document document;
static fm1_mod_player player;
static fm1_mod_editor editor;
static fm1_mod_library library;
static fm1_tracker_view view;
static fm1_tracker_view_context context;
static fm1_bank bank;
static uint8_t bank_bytes[128u*1024u];

static int load_bank(const char *path){
    FILE *file=fopen(path,"rb");size_t length;
    if(!file)return -1;length=fread(bank_bytes,1,sizeof(bank_bytes),file);
    if(ferror(file) || !feof(file)){fclose(file);return -1;}
    fclose(file);return fm1_bank_load(&bank,bank_bytes,(uint32_t)length);
}

static int load_song(const char *path){
    FILE *file=fopen(path,"rb");long length;size_t count;
    if(!file)return -1;
    if(fseek(file,0,SEEK_END) || (length=ftell(file))<0 ||
       (unsigned long)length>FM1_MOD_CAPACITY || fseek(file,0,SEEK_SET)){
        fclose(file);return -1;
    }
    count=fread(document.bytes,1,(size_t)length,file);
    fclose(file);
    return count!=(size_t)length?-1:fm1_mod_open(&document,count);
}

static void render_audio(unsigned blocks){
    int16_t stereo[FM1_MOD_BLOCK*2];
    while(blocks--)fm1_mod_render(&player,stereo,FM1_MOD_BLOCK);
}

static int screenshot(const char *directory,const char *name,unsigned page,
                      unsigned running,unsigned stereo){
    char path[2048];FILE *file;unsigned y,x;
    uint8_t pixels[240*3*2],rgb[240*3*3];
    int length=snprintf(path,sizeof(path),"%s/%s.ppm",directory,name);
    if(length<0 || (size_t)length>=sizeof(path))return -1;
    fm1_tracker_view_update(&view,&document,&player,&editor,&library,
                            page,running,stereo,0);
    fm1_tracker_view_context_apply(&view,&document,&editor,&context);
    file=fopen(path,"wb");if(!file)return -1;
    if(fprintf(file,"P6\n240 240\n255\n")<0){fclose(file);return -1;}
    for(y=0;y<240;y+=3){
        fm1_tracker_view_render(&view,y,3,pixels);
        for(x=0;x<240*3;x++){
            unsigned value=((unsigned)pixels[x*2]<<8)|pixels[x*2+1];
            rgb[x*3]=(uint8_t)(((value>>12)&15)*17);
            rgb[x*3+1]=(uint8_t)(((value>>7)&15)*17);
            rgb[x*3+2]=(uint8_t)(((value>>1)&15)*17);
        }
        if(fwrite(rgb,1,sizeof(rgb),file)!=sizeof(rgb)){fclose(file);return -1;}
    }
    if(fclose(file))return -1;
    printf("%s\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\n",name,page,
           running,editor.edit,editor.field,
           running?player.current_order:editor.order,
           running?player.current_row:editor.row,editor.channel,editor.sample,
           context.arranger,context.bank_open);
    return 0;
}

static void cursor(unsigned row,unsigned channel,unsigned sample,unsigned write){
    fm1_mod_player_start(&player,&document);fm1_mod_player_stop(&player);
    memset(&context,0,sizeof(context));
    fm1_mod_editor_init(&editor,&document);
    editor.order=0;editor.row=(uint8_t)row;editor.channel=(uint8_t)channel;
    editor.sample=(uint8_t)sample;editor.edit=(uint8_t)write;editor.octave=-1;
}

int main(int argc,char **argv){
    static const char *names[]={"FM1 STARTER","STATE OF THE ART","3D DEMO II","9 FINGERS"};
    unsigned i;
    if(argc!=4){fprintf(stderr,"Usage: %s <four-channel MOD> <output directory> <Active Bank>\n",argv[0]);return 2;}
    if(load_song(argv[1])){fprintf(stderr,"Cannot open four-channel MOD: %s\n",argv[1]);return 1;}
    if(load_bank(argv[3])){fprintf(stderr,"Cannot open Active Bank: %s\n",argv[3]);return 1;}
    memset(&library,0,sizeof(library));library.count=4;library.ready=1;library.selected=1;
    for(i=0;i<4;i++){
        snprintf(library.entries[i].name,sizeof(library.entries[i].name),"%s",names[i]);
        library.entries[i].size=i==1?(uint32_t)document.length:0;
    }

    cursor(16,0,1,0);
    if(fm1_mod_player_start_at(&player,&document,0,16))return 1;
    player.stereo=1;render_audio(32);
    if(screenshot(argv[2],"playing-song",0,1,1))return 1;

    cursor(18,2,7,0);
    if(fm1_mod_preview_start(&player,&document,7,48,2))return 1;
    render_audio(32);
    if(screenshot(argv[2],"edit-audition",0,0,1))return 1;

    cursor(18,2,7,1);
    if(fm1_mod_editor_note(&editor,60))return 1;
    editor.row=18; /* Scroll back to the newly entered note. */
    if(fm1_mod_preview_start(&player,&document,7,60,2))return 1;
    render_audio(32);
    if(screenshot(argv[2],"write-note",0,0,1))return 1;

    editor.field=FM1_FIELD_SAMPLE;
    if(screenshot(argv[2],"write-sample",0,0,1))return 1;
    editor.field=FM1_FIELD_EFFECT;
    if(screenshot(argv[2],"write-effect",0,0,1))return 1;
    editor.field=FM1_FIELD_PARAM;
    if(screenshot(argv[2],"write-param",0,0,1))return 1;

    cursor(16,0,1,1);fm1_mod_editor_block_start(&editor);
    editor.row=20;editor.channel=2;fm1_mod_editor_block_update(&editor);
    if(screenshot(argv[2],"block-selection",0,0,1))return 1;

    cursor(18,1,3,0);context.arranger=1;editor.order=(uint8_t)(document.orders>3?3:document.orders-1);
    if(screenshot(argv[2],"song-arranger",0,0,1))return 1;

    cursor(18,1,5,0);context.bank_open=1;context.bank_count=bank.count;context.bank_index=(uint16_t)(bank.count>19?19:bank.count-1);
    {fm1_bank_entry entry;
        if(fm1_bank_entry_get(&bank,context.bank_index,&entry))return 1;
        snprintf(context.bank_name,sizeof(context.bank_name),"%.19s",entry.name);
        if(fm1_mod_preview_pcm(&player,entry.pcm,entry.length,entry.volume,
            (unsigned)entry.finetune&15u,entry.loop_start,entry.loop_length,48,editor.channel))return 1;
        render_audio(8);
    }
    if(screenshot(argv[2],"bank-audition",0,0,1))return 1;

    cursor(18,0,1,1);context.quick_modifier=1;
    snprintf(context.notice,sizeof(context.notice),"Q1 BOUND S01 TIGHT KICK");
    if(screenshot(argv[2],"quick-sample",0,0,1))return 1;

    cursor(18,2,7,0);
    if(screenshot(argv[2],"sample-list",2,0,1) ||
       screenshot(argv[2],"usb-file-chooser",1,0,1))return 1;

    cursor(16,1,2,0);
    if(fm1_mod_player_start_pattern_at(&player,&document,0,16))return 1;
    player.stereo=1;render_audio(32);
    if(screenshot(argv[2],"playing-pattern",0,2,1))return 1;

    cursor(0,0,1,0);
    if(screenshot(argv[2],"boundary-row00",0,0,0))return 1;
    cursor(63,3,9,1);
    if(screenshot(argv[2],"boundary-row63",0,0,0))return 1;
    return 0;
}
