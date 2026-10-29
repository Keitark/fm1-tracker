#include "build_identity.h"

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline,used))
#endif
int fm1_tracker_fwinfo(const char *command, char *response, size_t capacity){
    static const char query[]="FWINFO ";
    static const char prefix[]="FM1FW/1 MOD-EDITOR/1 ";
    static const char hex[]="0123456789abcdef";
    const volatile uint8_t *identity=fm1_tracker_build_id;
    unsigned i,position=0;
    if(!command || !response || capacity<FM1_TRACKER_FWINFO_RESPONSE_BYTES)return 0;
    for(i=0;i<sizeof(query)-1u;i++)if(command[i]!=query[i])return 0;
    for(i=0;i<FM1_TRACKER_FWINFO_NONCE_BYTES*2u;i++){
        unsigned c=(unsigned char)command[sizeof(query)-1u+i];
        if(!((c>='0' && c<='9') || (c>='a' && c<='f')))return 0;
    }
    if(command[sizeof(query)-1u+FM1_TRACKER_FWINFO_NONCE_BYTES*2u])return 0;
    for(i=0;i<sizeof(prefix)-1u;i++)response[position++]=prefix[i];
    for(i=0;i<FM1_TRACKER_BUILD_ID_BYTES;i++){
        unsigned byte=identity[i];
        response[position++]=hex[byte>>4];response[position++]=hex[byte&15u];
    }
    response[position++]=' ';
    for(i=0;i<FM1_TRACKER_FWINFO_NONCE_BYTES*2u;i++)
        response[position++]=command[sizeof(query)-1u+i];
    response[position++]='\n';response[position]='\0';
    return 1;
}
