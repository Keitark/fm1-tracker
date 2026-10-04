/* Volatile catalog supplied by the PC. The device never writes its flash. */
#include "library.h"
#include <stdio.h>
#include <string.h>

static int digit(char c){
    if(c>='0' && c<='9')return c-'0';
    if(c>='a' && c<='f')return c-'a'+10;
    if(c>='A' && c<='F')return c-'A'+10;
    return -1;
}
static int parse_hex(const char *s,unsigned n,uint32_t *value){
    unsigned i;uint32_t result=0;
    for(i=0;i<n;i++){int d=digit(s[i]);if(d<0)return 0;result=(result<<4)|(unsigned)d;}
    *value=result;return 1;
}
static int safe_char(unsigned c){return (c>='A' && c<='Z') || (c>='0' && c<='9') || c==' ' || c=='-' || c=='.';}

int fm1_mod_library_line(fm1_mod_library *lib,const char *line,fm1_mod_reply reply,void *ctx){
    size_t n;uint32_t size,id;char out[48];unsigned i;
    if(strncmp(line,"LIB ",4))return 0;
    if(!lib || !reply)return 1;
    n=strlen(line);
    if(!strcmp(line,"LIB RESET")){
        memset(lib,0,sizeof(*lib));reply(ctx,"OK LIB RESET\n");return 1;
    }
    if(n==57 && !memcmp(line,"LIB ADD ",8) && line[16]==' ' &&
       parse_hex(line+8,8,&size) && size>=2108 && size<=FM1_MOD_CAPACITY &&
       !lib->ready && lib->count<FM1_MOD_LIBRARY_MAX){
        fm1_mod_library_entry *entry=&lib->entries[lib->count];
        for(i=0;i<20;i++){
            int hi=digit(line[17+2*i]),lo=digit(line[18+2*i]);unsigned c;
            if(hi<0 || lo<0){reply(ctx,"ERR LIB NAME\n");return 1;}
            c=(unsigned)((hi<<4)|lo);
            if(!safe_char(c)){reply(ctx,"ERR LIB NAME\n");return 1;}
            entry->name[i]=(char)c;
        }
        entry->name[20]=0;
        for(i=20;i>0 && entry->name[i-1]==' ';i--)entry->name[i-1]=0;
        if(!entry->name[0]){reply(ctx,"ERR LIB NAME\n");return 1;}
        entry->size=size;lib->count++;
        reply(ctx,"OK LIB ADD\n");return 1;
    }
    if(!strcmp(line,"LIB READY")){
        if(!lib->count){reply(ctx,"ERR LIB EMPTY\n");return 1;}
        lib->ready=1;reply(ctx,"OK LIB READY\n");return 1;
    }
    if(!strcmp(line,"LIB POLL")){
        if(lib->pending){snprintf(out,sizeof(out),"LIB PICK %04X %02X\n",lib->ticket,lib->selected);reply(ctx,out);}
        else reply(ctx,"LIB IDLE\n");
        return 1;
    }
    if(n==13 && (!memcmp(line,"LIB DONE ",9) || !memcmp(line,"LIB FAIL ",9)) &&
       parse_hex(line+9,4,&id) && lib->pending && id==lib->ticket){
        lib->failed=line[4]=='F';lib->pending=0;
        reply(ctx,lib->failed?"OK LIB FAIL\n":"OK LIB DONE\n");return 1;
    }
    reply(ctx,"ERR LIB COMMAND\n");return 1;
}

void fm1_mod_library_move(fm1_mod_library *lib,int step){
    int n;
    if(!lib || !lib->ready || !lib->count || lib->pending)return;
    n=((int)lib->selected+step)%(int)lib->count;
    if(n<0)n+=lib->count;
    lib->selected=(uint8_t)n;
}
int fm1_mod_library_pick(fm1_mod_library *lib){
    if(!lib || !lib->ready || !lib->count || lib->pending)return -1;
    lib->ticket++;if(!lib->ticket)lib->ticket=1;
    lib->pending=1;lib->failed=0;return 0;
}
