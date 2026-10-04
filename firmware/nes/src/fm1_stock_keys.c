#include "fm1_stock_keys.h"
#include <string.h>

/* FM-1_010 runtime 0x02046334: high nibble row, low nibble column.
   gpio sampling: PA0, PA5, PA6, PA7, PA8, PB7. See STOCK_BOARD_PROFILE.md. */
static const uint8_t stock_button_cells[41] = {
    0x14,0x24,0x71,0x51,0x31,0x11,0xa2,0x92,0x81,0x61,0x41,0x21,0x02,0x82,
    0x44,0x34,0x64,0x54,0x84,0x74,0x94,0xa4,0x04,0x13,0x23,0x33,0x43,0x53,
    0x63,0x73,0x83,0x93,0xa3,0x03,0x12,0x22,0x32,0x42,0x52,0x72,0x62
};
uint8_t fm1_stock_pack_columns(uint32_t a,uint32_t b) {
    return (uint8_t)((a&1u)|((a>>4)&0x1eu)|((b>>2)&0x20u));
}
uint64_t fm1_stock_decode_keys(const uint8_t rows[FM1_STOCK_SCAN_ROWS]) {
    unsigned i;uint64_t pressed=0;
    if(!rows)return 0;
    for(i=0;i<FM1_KEY_COUNT;++i) {
        unsigned cell=stock_button_cells[i];
        if(!(rows[cell>>4]&(1u<<(cell&15u))))pressed|=UINT64_C(1)<<i;
    }
    return pressed;
}
int fm1_stock_assign_note_keys(fm1_board_config *c) {
    /* Stock note handler: pitch = slot - 14 + 53 + octave*12 + transpose.
       Slots14..40 run F..high G. Famicom layout: directions at the left,
       B/A on far-right high F/G (slots38/40), not the middle octave.
       Pad order: A, B, Select, Start, Up, Down, Left, Right. */
    static const uint8_t pad_slots[8]={40,38,19,22,17,16,14,18};
    if(!c)return FM1_NES_INVALID;
    memcpy(c->key_for_pad,pad_slots,sizeof(pad_slots));
    return 0;
}
