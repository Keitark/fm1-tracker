#ifndef FM1_DISPLAY_TEST_H
#define FM1_DISPLAY_TEST_H
#include <stdint.h>
#if defined(FM1_LCD_DIRECT) && !defined(FM1_LCD_RGB444)
#error Direct LCD packing requires RGB444
#endif
#ifdef FM1_LCD_RGB444
#ifndef FM1_LCD_ASYNC
#error RGB444 requires the asynchronous NES LCD path
#endif
#define FM1_LCD_WIRE_BPP 12
#define FM1_LCD_WIRE_ROW_BYTES 360u
#define FM1_LCD_FORMAT_NAME "RGB444"
#else
#define FM1_LCD_WIRE_BPP 16
#define FM1_LCD_WIRE_ROW_BYTES 480u
#define FM1_LCD_FORMAT_NAME "RGB565"
#endif
#define FM1_LCD_WIRE_STRIP_BYTES (8u*FM1_LCD_WIRE_ROW_BYTES)
#define FM1_LCD_WIRE_FRAME_BYTES (240u*FM1_LCD_WIRE_ROW_BYTES)
#if defined(FM1_LCD_SPI15) && defined(FM1_LCD_SPI30)
#error Select only one LCD SPI clock experiment
#endif
#if defined(FM1_LCD_SPI15) || defined(FM1_LCD_SPI30)
#define FM1_LCD_FAST_SPI 1
#ifndef FM1_LCD_ASYNC
#error Fast SPI requires the asynchronous NES LCD path
#endif
#ifdef FM1_LCD_SPI30
#define FM1_LCD_STREAM_BAUD 1
#else
#define FM1_LCD_STREAM_BAUD 3
#endif
#else
#define FM1_LCD_STREAM_BAUD 4
#endif
int fm1_display_test_init(void);
int fm1_display_test_frame(uint32_t elapsed_ms);
void fm1_display_test_stop(void);
#if defined(FM1_NES_PLAYER) || defined(FM1_MDX_PLAYER) || defined(FM1_MOD_PLAYER)
#include <stddef.h>
/* Synchronous stream through the already initialized stock-sequence driver. */
int fm1_display_write(int data,const uint8_t *,size_t);
#ifdef FM1_LCD_ASYNC
typedef struct {
    uint32_t submitted,completed,busy_skips,irqs,bytes,errors;
    uint32_t active,pending;
} fm1_lcd_async_stats;
/* Single task producer; the IRQ only advances the active immutable buffer. */
int fm1_display_async_start(void);
int fm1_display_async_begin(int wanted);
int fm1_display_async_rows(unsigned y,unsigned rows,const uint8_t *wire);
#ifdef FM1_LCD_DIRECT
int fm1_display_async_native_rows(unsigned y,unsigned rows,const uint16_t *,unsigned crop);
#endif
int fm1_display_async_service(void);
void fm1_display_async_snapshot(fm1_lcd_async_stats *);
#endif
#endif
extern volatile uint32_t fm1_display_stage;
extern volatile int fm1_display_error;
#ifdef FM1_LCD_STOCK_DMA
/* Fixed read-only MMIO snapshot, called synchronously by the LCD worker.
 * Phases: 0 before setup, 1 configured, 2 display-on, 3 failure, 4 stopped. */
#ifdef FM1_LCD_STOCK_SEQUENCE
#define FM1_LCD_REGISTER_COUNT 20
#else
#define FM1_LCD_REGISTER_COUNT 18
#endif
void fm1_display_snapshot(unsigned phase,const uint32_t registers[FM1_LCD_REGISTER_COUNT]);
#endif
#endif
