#ifndef FM1_FAKE_SDK_SERVICES_H
#define FM1_FAKE_SDK_SERVICES_H
#include <stdint.h>
#include <windows.h>
typedef struct {SRWLOCK native;} spinlock_t;
#define ___interrupt
#define IRQ_ALNK_IDX 11
unsigned fm1_fake_irq_save(void);
void fm1_fake_irq_restore(unsigned);
void fm1_fake_lock(spinlock_t *);
void fm1_fake_unlock(spinlock_t *);
void fm1_fake_barrier(void);
#define local_irq_save(f) ((f)=fm1_fake_irq_save())
#define local_irq_restore(f) fm1_fake_irq_restore(f)
#define arch_spin_lock(l) fm1_fake_lock(l)
#define arch_spin_unlock(l) fm1_fake_unlock(l)
uint32_t timer_get_ms(void);
void os_time_dly(int);
void wdt_clear(void);
int clk_get(const char *);
void request_irq(unsigned char,unsigned char,void (*)(void),unsigned char);
void unrequest_irq(unsigned char,unsigned char);
void bit_clr_ie(unsigned char,unsigned char);
#endif
