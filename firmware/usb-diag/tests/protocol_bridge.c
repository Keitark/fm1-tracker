/* Offline integration harness: exact firmware parser over stdin/stdout. */
#include "protocol.h"
#include <stdio.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
static void emit(void *ctx,const char *s){(void)ctx;fputs(s,stdout);fflush(stdout);}
int main(void) {
    fm1_diag_protocol p;int c;fm1_diag_reset(&p);
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    while((c=getchar())!=EOF) {
        uint8_t b=(uint8_t)c;
        fm1_diag_feed(&p,&b,1,0,emit,NULL);
    }
    return 0;
}
