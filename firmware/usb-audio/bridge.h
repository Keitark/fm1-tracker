/* SPDX-License-Identifier: GPL-3.0-only
 * Reused from Keitark/fm1-mdx at 4141057; see licenses/fm1-mdx-GPL-3.0.txt. */
#ifndef FM1_UAC_BRIDGE_H
#define FM1_UAC_BRIDGE_H
#include <stdint.h>
#include <stddef.h>
#define FM1_UAC_FRAMES 1024
#define FM1_UAC_PACKET 192
#define FM1_MIC_ATTACK 1024u
#define FM1_MIC_RELEASE 512u
#define FM1_MIC_HOLD 441u /* 10 ms at the existing 44.1 kHz DAC cadence. */
typedef struct {
    int16_t data[FM1_UAC_FRAMES][2];
    uint32_t rd,wr,phase,underruns,overruns;
    unsigned primed;
} fm1_uac_fifo;
typedef struct {
    fm1_uac_fifo playback,capture;
    unsigned out_active,in_active;
    unsigned playback_ramp,playback_tail;
    int16_t playback_last[2],playback_release[2];
    uint32_t rx_packets,tx_packets,bad_packets;
    unsigned mic_enabled,mic_hold,mic_active;
} fm1_uac_bridge;
void fm1_uac_microphone(fm1_uac_bridge *,int on);
void fm1_uac_stream(fm1_uac_bridge *,unsigned direction,int on);
int fm1_uac_receive(fm1_uac_bridge *,const uint8_t *,size_t);
void fm1_uac_dac(fm1_uac_bridge *,int32_t *stereo,unsigned frames);
size_t fm1_uac_transmit(fm1_uac_bridge *,uint8_t *,size_t);
extern const uint8_t fm1_uac_descriptor[173];
int fm1_uac_interface_request(uint8_t type,uint8_t request,uint16_t value,uint16_t index,uint16_t length);
#endif
