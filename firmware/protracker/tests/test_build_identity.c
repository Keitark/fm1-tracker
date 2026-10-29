#include "build_identity.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

const uint8_t fm1_tracker_build_id[32]={
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
    0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
    0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
    0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
};

#define NONCE_A "0123456789abcdef0123456789abcdef"
#define NONCE_B "fedcba98765432100011223344556677"
#define RESPONSE_PREFIX "FM1FW/1 MOD-EDITOR/1 " \
    "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f "

typedef struct {
    unsigned char before[16];
    char data[144];
    unsigned char after[16];
} guarded_response;
typedef struct {
    unsigned char before[16];
    char data[64];
    unsigned char after[16];
} guarded_command;

static void reject_unchanged(const char *command,size_t bytes,size_t capacity){
    guarded_command input,saved_input;
    guarded_response output,saved_output;
    memset(&input,0xa5,sizeof(input));
    memset(&output,0x5a,sizeof(output));
    assert(bytes<=sizeof(input.data) && capacity<=sizeof(output.data));
    if(command)memcpy(input.data,command,bytes);
    saved_input=input;saved_output=output;
    assert(fm1_tracker_fwinfo(command?input.data:NULL,output.data,capacity)==0);
    assert(!memcmp(&input,&saved_input,sizeof(input)));
    assert(!memcmp(&output,&saved_output,sizeof(output)));
}

static void header_sizes_match_wire_contract(void){
    assert(FM1_TRACKER_BUILD_ID_BYTES==32);
    assert(FM1_TRACKER_FWINFO_NONCE_BYTES==16);
    assert(FM1_TRACKER_FWINFO_RESPONSE_BYTES==120);
    assert(sizeof(RESPONSE_PREFIX NONCE_A "\n")==120);
}

static void exact_build_id_and_nonce_echo(void){
    static const char *const commands[]={"FWINFO " NONCE_A,"FWINFO " NONCE_B};
    static const char *const answers[]={RESPONSE_PREFIX NONCE_A "\n",RESPONSE_PREFIX NONCE_B "\n"};
    unsigned i,j;
    for(i=0;i<2;i++)for(j=0;j<2;j++){
        guarded_command input,saved_input;
        guarded_response output,expected;
        size_t capacity=j?sizeof(output.data):FM1_TRACKER_FWINFO_RESPONSE_BYTES;
        memset(&input,0xa5,sizeof(input));
        memcpy(input.data,commands[i],40);saved_input=input;
        memset(&output,0x5a,sizeof(output));expected=output;
        memcpy(expected.data,answers[i],120);
        assert(fm1_tracker_fwinfo(input.data,output.data,capacity)==1);
        assert(!memcmp(&output,&expected,sizeof(output)));
        assert(!memcmp(&input,&saved_input,sizeof(input)));
        assert(strlen(output.data)==119 && output.data[118]=='\n' && output.data[119]=='\0');
    }
}

static void every_undersized_capacity_is_rejected(void){
    static const char command[]="FWINFO " NONCE_A;
    size_t capacity;
    for(capacity=0;capacity<120;capacity++)
        reject_unchanged(command,sizeof(command),capacity);
}

static void command_syntax_is_exact(void){
    static const char *const invalid[]={
        "","HELLO","STATUS","FWINFO","FWINFO ",
        "XWINFO " NONCE_A,"fwinfo " NONCE_A,"FWINFo " NONCE_A,
        "FWINFOX " NONCE_A," FWINFO " NONCE_A,"\tFWINFO " NONCE_A,
        "FWINFO" NONCE_A,"FWINFO  " NONCE_A,"FWINFO\t" NONCE_A,
        "FWINFO " NONCE_A "0","FWINFO " NONCE_A " ",
        "FWINFO " NONCE_A "\t","FWINFO " NONCE_A "\n",
        "FWINFO " NONCE_A "\r","FWINFO " NONCE_A "\r\n"
    };
    unsigned i;
    for(i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)
        reject_unchanged(invalid[i],strlen(invalid[i])+1,120);
}

static void nonce_requires_only_lowercase_hex(void){
    static const unsigned char invalid[]={'A','F','g','/',' ','\t',0x80,0xff};
    char command[]="FWINFO " NONCE_A;
    unsigned i,j;
    for(i=0;i<32;i++)for(j=0;j<sizeof(invalid);j++){
        memcpy(command,"FWINFO " NONCE_A,sizeof(command));
        ((unsigned char *)command)[7+i]=invalid[j];
        reject_unchanged(command,sizeof(command),120);
    }
}

static void embedded_nul_and_truncated_nonce_are_rejected(void){
    char command[]="FWINFO " NONCE_A;
    unsigned i;
    for(i=0;i<39;i++){
        memcpy(command,"FWINFO " NONCE_A,sizeof(command));
        command[i]='\0';
        reject_unchanged(command,sizeof(command),120);
    }
}

static void null_arguments_and_zero_capacity_are_rejected(void){
    char command[]="FWINFO " NONCE_A,saved[sizeof(command)],sentinel='x';
    memcpy(saved,command,sizeof(command));
    reject_unchanged(NULL,0,120);
    assert(fm1_tracker_fwinfo(command,NULL,120)==0);
    assert(fm1_tracker_fwinfo(NULL,NULL,120)==0);
    assert(fm1_tracker_fwinfo(command,NULL,0)==0);
    assert(fm1_tracker_fwinfo(NULL,NULL,0)==0);
    assert(fm1_tracker_fwinfo(command,&sentinel+1,0)==0 && sentinel=='x');
    assert(!memcmp(command,saved,sizeof(command)));
}

int main(void){
    header_sizes_match_wire_contract();exact_build_id_and_nonce_echo();
    every_undersized_capacity_is_rejected();command_syntax_is_exact();
    nonce_requires_only_lowercase_hex();embedded_nul_and_truncated_nonce_are_rejected();
    null_arguments_and_zero_capacity_are_rejected();
    puts("tracker FWINFO exact identity, nonce, parser and buffer bounds passed");return 0;
}
