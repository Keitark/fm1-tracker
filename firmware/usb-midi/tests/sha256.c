/* SPDX-License-Identifier: Apache-2.0 */
#include "fm1_sha256.h"
#include <stdio.h>
#include <string.h>

static int check(const uint8_t *data,unsigned n,unsigned stride,const char *expected){
    fm1_sha256 s;uint8_t digest[32];char text[65];unsigned i,k;
    fm1_sha256_init(&s);
    for(i=0;i<n;i+=k){k=n-i;if(k>stride)k=stride;fm1_sha256_update(&s,data+i,k);}
    fm1_sha256_final(&s,digest);
    for(i=0;i<32;i++)sprintf(text+2*i,"%02x",digest[i]);
    if(strcmp(text,expected)){fprintf(stderr,"SHA-256 vector mismatch: %u/%u\n",n,stride);return 1;}
    return 0;
}
int main(void){
    static uint8_t million[1000000];unsigned stride;
    const char *abc="abc";
    const char *longer="abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    memset(million,'a',sizeof(million));
    if(check((const uint8_t *)"",0,1,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"))return 1;
    for(stride=1;stride<=65;stride++){
        if(check((const uint8_t *)abc,3,stride,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))return 2;
        if(check((const uint8_t *)longer,(unsigned)strlen(longer),stride,"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"))return 3;
    }
    if(check(million,sizeof(million),1024,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"))return 4;
    return 0;
}
