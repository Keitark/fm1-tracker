#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
void __wrap_os_init(void);
static int stage, display_result, os_calls;
void fm1_board_power_init(void) {CHECK(stage==0);stage=1;}
int fm1_display_test_init(void) {CHECK(stage==1);stage=2;return display_result;}
void __real_os_init(void) {CHECK(stage==2);stage=3;++os_calls;}
int main(void) {
    __wrap_os_init();CHECK(stage==3 && os_calls==1);
    stage=0;display_result=-2;
    __wrap_os_init();CHECK(stage==3 && os_calls==2);
    puts("pre-OS power/display order and original OS continuation passed (mock SDK)");
    return 0;
}
