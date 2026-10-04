#ifndef FM1_FAKE_BOOT_APP_H
#define FM1_FAKE_BOOT_APP_H
#include <stdint.h>
struct irq_info {int irq,priority,cpu;};
struct task_info {const char *name;int priority,stack,queue,extra;};
uint32_t timer_get_ms(void);
void os_time_dly(int ticks);
int task_create(void (*task)(void *),void *arg,const char *name);
#endif
