#ifndef FM1_FORGE_FAKE_HAL_H
#define FM1_FORGE_FAKE_HAL_H
#include <stdint.h>
#include <stddef.h>
typedef uint8_t u8;
typedef struct {unsigned held;} spinlock_t;
typedef void (*irq_fn)(void);
typedef void (*task_fn)(void *);
#define ___interrupt
#ifdef _MSC_VER
#define __attribute__(value)
#endif
#define local_irq_save(f) do{f=0;}while(0)
#define local_irq_restore(f) ((void)(f))
#define IRQ_ALNK_IDX 11
#define IRQ_SPI2_IDX 37
#define IIS_PORTC 2
struct iis_platform_data {unsigned port_sel,channel_out,data_width,mclk_output,sr_points;};
void arch_spin_lock(spinlock_t *);
void arch_spin_unlock(spinlock_t *);
int clk_get(const char *);
int system_clock_set(uint32_t);
uint32_t timer_get_ms(void);
int sys_usec_timer_add(void *,void (*)(void *),unsigned,unsigned,unsigned);
void sys_usec_timer_del(int);
int task_create(task_fn,void *,const char *);
void request_irq(unsigned,unsigned,irq_fn,unsigned);
void unrequest_irq(unsigned,unsigned);
void bit_clr_ie(unsigned,unsigned);
void os_time_dly(int);
void wdt_clear(void);
int iis_open(struct iis_platform_data *,unsigned);
void iis_close(unsigned);
void iis_set_dec_data_handler(unsigned,void (*)(void *,u8 *,int,u8),void *);
int iis_set_sample_rate(unsigned,unsigned);
void iis_channel_on(unsigned,unsigned);
void iis_channel_off(unsigned,unsigned);
void iis_irq_handler(unsigned);
#endif
