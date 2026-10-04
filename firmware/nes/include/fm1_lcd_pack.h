#ifndef FM1_LCD_PACK_H
#define FM1_LCD_PACK_H
#include <stdint.h>
/* Caller validates buffers/rows/crop: dst has360*rows bytes, src has256*rows
 * native RGB565 pixels; crop is0 or1. No IO/allocation/shared state. */
void fm1_lcd_pack_native_rgb444(uint8_t *dst,const uint16_t *src,unsigned rows,unsigned crop);
#endif
