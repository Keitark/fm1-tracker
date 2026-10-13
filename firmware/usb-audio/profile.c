/* SPDX-License-Identifier: GPL-3.0-only
 * Reused from Keitark/fm1-mdx at 4141057; see licenses/fm1-mdx-GPL-3.0.txt. */
#include "bridge.h"
/* CDC owns0/1, OUT2, IN2(notification), IN3(data). UAC owns2..4 and EP1.
 * UAC1 fixed stereo PCM48k/16-bit; no unimplemented hardware volume controls. */
const uint8_t fm1_uac_descriptor[173]={
    8,11,2,3,1,1,0,0,
    9,4,2,0,0,1,1,0,0,
    10,36,1,0,1,52,0,2,3,4,
    12,36,2,1,1,1,0,2,3,0,0,0,
    9,36,3,2,1,3,0,1,0,
    /* Conventional virtual line capture category. PCM comes from the application
       synth; this terminal does not add an analog input. */
    12,36,2,3,3,6,0,2,3,0,0,0,
    9,36,3,4,1,1,0,3,0,
    9,4,3,0,0,1,2,0,0,
    9,4,3,1,1,1,2,0,0,
    7,36,1,1,1,1,0,
    11,36,2,1,2,2,16,1,0x80,0xbb,0,
    9,5,1,9,192,0,1,0,0,
    7,37,1,0,0,0,0,
    9,4,4,0,0,1,2,0,0,
    9,4,4,1,1,1,2,0,0,
    7,36,1,4,1,1,0,
    11,36,2,1,2,2,16,1,0x80,0xbb,0,
    9,5,0x81,13,192,0,1,0,0,
    7,37,1,0,0,0,0
};
int fm1_uac_interface_request(uint8_t t,uint8_t r,uint16_t v,uint16_t i,uint16_t n) {
    if(i<2 || i>4)return 0;
    if(r==11)return t==1 && n==0 && v<=(i==2?0:1);
    if(r==10)return t==0x81 && n==1 && v==0;
    if(r==0)return t==0x81 && n==2 && v==0;
    return 0;
}
