#ifndef FM1_STOCK_KEYS_H
#define FM1_STOCK_KEYS_H
#include "fm1_board.h"
#define FM1_STOCK_SCAN_ROWS 11u
/* Pure stock-derived decoding. No GPIO, SPI, IRQ, timing or debounce here.
   rows must be one coherent scan, low six bits in stock GPIO packing order,
   active-low. Slot numbers match FM-1_010 button table, NOT GPIO numbers. */
uint8_t fm1_stock_pack_columns(uint32_t porta_in,uint32_t portb_in);
uint64_t fm1_stock_decode_keys(const uint8_t rows[FM1_STOCK_SCAN_ROWS]);
/* Famicom layout using stock slots: low F/G/A=Left/Down/Right, G#=Up,
   A#/C#=Select/Start; far-right high F/G=B/A. Physical remap needs bench test.
   Only changes key_for_pad. Does not install or enable a physical scanner. */
int fm1_stock_assign_note_keys(fm1_board_config *);
#endif
