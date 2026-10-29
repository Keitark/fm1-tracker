#include "library.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
#include <stdio.h>

static char answer[80];
static void reply(void *context,const char *text){
    (void)context;snprintf(answer,sizeof(answer),"%s",text);
}
int main(void){
    fm1_mod_library lib={0};
    assert(fm1_mod_library_save(&lib)<0);
    lib.ready=1;lib.count=1;lib.last_ms=100;
    assert(!fm1_mod_library_save(&lib) && lib.saving && lib.pending);
    assert(fm1_mod_library_pick(&lib)<0 && fm1_mod_library_save(&lib)<0);
    fm1_mod_library_line(&lib,"LIB POLL",reply,0);
    assert(!strcmp(answer,"LIB SAVE 0001\n"));
    fm1_mod_library_line(&lib,"LIB DONE 0000",reply,0);
    assert(lib.pending);
    fm1_mod_library_line(&lib,"LIB DONE 0001",reply,0);
    assert(!lib.pending && !lib.failed);
    assert(!fm1_mod_library_save(&lib));
    fm1_mod_library_line(&lib,"LIB FAIL 0002",reply,0);
    assert(!lib.pending && lib.failed);
    lib.ticket=65535;
    assert(!fm1_mod_library_pick(&lib) && lib.ticket==1 && !lib.saving);
    fm1_mod_library_tick(&lib,10100);assert(lib.pending);
    fm1_mod_library_tick(&lib,10101);assert(!lib.pending && lib.failed);
    lib.last_ms=UINT32_MAX-100;
    assert(!fm1_mod_library_save(&lib));
    fm1_mod_library_tick(&lib,9900);assert(!lib.pending && lib.failed);
    puts("library save request, acknowledgement, failure and wrap-safe timeout passed");
    return 0;
}
