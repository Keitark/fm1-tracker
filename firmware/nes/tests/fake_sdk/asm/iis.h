/* Host test declaration shim. Target builds use the real pinned SDK header. */
#ifndef FM1_FAKE_IIS_H
#define FM1_FAKE_IIS_H
#include <stdint.h>
typedef uint8_t u8;
#define IIS_PORTC 1
struct iis_platform_data {
    u8 channel_in,channel_out,port_sel,data_width,mclk_output,slave_mode,
       update_edge,f32e,keep_alive,sel,width_16_to_24;
    uint16_t dump_points_num,sr_points;
};
int iis_open(struct iis_platform_data *,u8);
void iis_close(u8);
int iis_set_sample_rate(int,u8);
void iis_set_dec_data_handler(void *,void (*)(void *,u8 *,int,u8),u8);
void iis_channel_on(u8,u8);
void iis_channel_off(u8,u8);
void iis_irq_handler(u8);
#endif
