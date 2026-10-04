#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

void __wrap_boot_info_init(const volatile uint32_t *argument);
static uint32_t expected[6];
static unsigned calls;
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#c); exit(1); } } while(0)

void __real_boot_info_init(const volatile uint32_t *argument)
{
    unsigned i;
    ++calls;
    CHECK(((uintptr_t)argument&3)==0);
    for(i=0;i<6;++i) CHECK(argument[i]==expected[i]);
    for(i=6;i<23;++i) CHECK(argument[i]==0);
}

int main(void)
{
    unsigned i,run;
    uint32_t input[23];
    for(run=0;run<3;++run) {
        for(i=0;i<23;++i) input[i]=0xa5a50000u+(run<<8)+i;
        memcpy(expected,input,sizeof(expected));
        __wrap_boot_info_init(input);
        for(i=0;i<23;++i) CHECK(input[i]==0xa5a50000u+(run<<8)+i);
    }
#ifdef _WIN32
    {
        SYSTEM_INFO info;
        unsigned char *pages;
        uint32_t *bounded;
        DWORD old_protect;
        GetSystemInfo(&info);
        pages=VirtualAlloc(NULL,2*info.dwPageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        CHECK(pages!=NULL);
        bounded=(uint32_t *)(pages+info.dwPageSize-sizeof(expected));
        for(i=0;i<6;++i) bounded[i]=expected[i]=0x12340000u+i;
        CHECK(VirtualProtect(pages+info.dwPageSize,info.dwPageSize,PAGE_NOACCESS,&old_protect));
        CHECK(VirtualProtect(pages,info.dwPageSize,PAGE_READONLY,&old_protect));
        /* Any source write or source read beyond the 24-byte prefix faults. */
        __wrap_boot_info_init(bounded);
        CHECK(VirtualFree(pages,0,MEM_RELEASE));
    }
    CHECK(calls==4);
#else
    CHECK(calls==3);
#endif
    puts("boot compatibility prefix/extension contract passed");
    return 0;
}
