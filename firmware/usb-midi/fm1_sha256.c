/* SPDX-License-Identifier: Apache-2.0
 * Independent implementation of the SHA-256 algorithm in FIPS 180-4.
 * Used for a bounded, read-only baseline check, not firmware authentication.
 */
#include "fm1_sha256.h"
#include <string.h>

static const uint32_t constants[64]={
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};
static uint32_t rotate(uint32_t value,unsigned bits){return (value>>bits)|(value<<(32u-bits));}
static uint32_t big32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void compress(fm1_sha256 *s){
    uint32_t words[64],a,b,c,d,e,f,g,h,t,u;
    unsigned i;
    for(i=0;i<16;i++)words[i]=big32(s->block+4*i);
    for(i=16;i<64;i++){
        t=rotate(words[i-15],7)^rotate(words[i-15],18)^(words[i-15]>>3);
        u=rotate(words[i-2],17)^rotate(words[i-2],19)^(words[i-2]>>10);
        words[i]=words[i-16]+t+words[i-7]+u;
    }
    a=s->state[0];b=s->state[1];c=s->state[2];d=s->state[3];
    e=s->state[4];f=s->state[5];g=s->state[6];h=s->state[7];
    for(i=0;i<64;i++){
        t=h+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^((~e)&g))+constants[i]+words[i];
        u=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c));
        h=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;
    }
    s->state[0]+=a;s->state[1]+=b;s->state[2]+=c;s->state[3]+=d;
    s->state[4]+=e;s->state[5]+=f;s->state[6]+=g;s->state[7]+=h;
}
void fm1_sha256_init(fm1_sha256 *s){
    static const uint32_t initial[8]={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    memset(s,0,sizeof(*s));memcpy(s->state,initial,sizeof(initial));
}
void fm1_sha256_update(fm1_sha256 *s,const void *data,size_t count){
    const uint8_t *p=(const uint8_t *)data;
    size_t n;
    s->bytes+=(uint64_t)count;
    while(count){
        n=64u-s->used;if(n>count)n=count;
        memcpy(s->block+s->used,p,n);s->used+=(unsigned)n;p+=n;count-=n;
        if(s->used==64){compress(s);s->used=0;}
    }
}
void fm1_sha256_final(fm1_sha256 *s,uint8_t output[32]){
    uint8_t pad[128]={0x80};
    uint64_t bits=s->bytes*8u;
    unsigned i,length=s->used<56?56-s->used:120-s->used;
    for(i=0;i<8;i++)pad[length+i]=(uint8_t)(bits>>(56u-8u*i));
    fm1_sha256_update(s,pad,length+8u);
    for(i=0;i<32;i++)output[i]=(uint8_t)(s->state[i/4]>>(24u-8u*(i%4)));
    memset(s,0,sizeof(*s));
}
