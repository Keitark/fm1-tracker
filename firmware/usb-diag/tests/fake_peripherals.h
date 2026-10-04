#ifndef FM1_FAKE_PERIPHERALS_H
#define FM1_FAKE_PERIPHERALS_H
#include <stdint.h>
typedef unsigned char u8;
typedef unsigned spinlock_t;
typedef struct {unsigned created,count;} OS_SEM;
int os_sem_create(OS_SEM *,int);
int os_sem_post(OS_SEM *);
int os_sem_pend(OS_SEM *,int);
int os_sem_del(OS_SEM *,int);
int sys_usec_timer_add(void *,void (*)(void *),uint32_t,unsigned char,unsigned char);
void sys_usec_timer_del(int);
#define local_irq_save(flags) ((flags)=0)
#define local_irq_restore(flags) ((void)(flags))
#define ___interrupt
#ifdef _MSC_VER
#define __attribute__(x)
#endif
#define FM1_USB_CONTROLLER 0
#define IIS_PORTC 2
#define IRQ_ALNK_IDX 11
#define IRQ_SPI2_IDX 37
struct iis_platform_data {unsigned port_sel,channel_out,data_width,mclk_output,update_edge,f32e,sr_points;};
void arch_spin_lock(spinlock_t *);
void arch_spin_unlock(spinlock_t *);
int task_create(void (*)(void *),void *,const char *);
uint32_t timer_get_ms(void);
void os_time_dly(int);
int clk_get(const char *);
void wdt_clear(void);
int iis_open(struct iis_platform_data *,unsigned);
int iis_set_sample_rate(unsigned,unsigned);
void iis_set_dec_data_handler(unsigned,void (*)(void *,u8 *,int,u8),void *);
void iis_channel_on(unsigned,unsigned);
void iis_channel_off(unsigned,unsigned);
void iis_close(unsigned);
void iis_irq_handler(unsigned);
void request_irq(unsigned,unsigned,void (*)(void),unsigned);
void unrequest_irq(unsigned,unsigned);
void bit_clr_ie(unsigned,unsigned);
#endif
