/* SPDX-License-Identifier: Apache-2.0 */
#ifndef FM1_MIDI_UPDATE_FAKE_H
#define FM1_MIDI_UPDATE_FAKE_H
#include <stdint.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;
typedef struct {unsigned count;} OS_SEM;
int os_sem_create(OS_SEM *,unsigned);int os_sem_post(OS_SEM *);int os_sem_pend(OS_SEM *,unsigned);
#ifndef local_irq_save
#define local_irq_save(f) do{f=0;}while(0)
#define local_irq_restore(f) ((void)(f))
#endif
typedef enum {USB_HID_UPDATA=0x5a0d} UPDATA_TYPE;
typedef struct {u16 parm_crc,parm_type,parm_result,magic;u8 file_path[32],parm_priv[32];u32 ota_addr;u16 ext_arg_len,ext_arg_crc;} UPDATA_PARM;
typedef struct {int stu;u8 err_code;} update_ret_code_t;
typedef struct {
    void (*ch_init)(void (*)(void *),int (*)(void *));
    u16 (*f_open)(void);u16 (*f_read)(void *,u8 *,u16);int (*f_seek)(void *,u8,u32);
    u16 (*f_stop)(u8);int (*notify_update_content_size)(void *,u32);void (*ch_exit)(void *);
} update_op_api_t;
typedef struct {int type;void (*state_cbk)(int,u32,void *);const update_op_api_t *p_op_api;u8 task_en;} update_mode_info_t;
enum {UPDATE_TASK_INIT,UPDATE_CH_INIT,UPDATE_CH_SUCESS_REPORT,UPDATE_CH_EXIT};
int app_active_update_task_init(update_mode_info_t *);
void update_mode_api_v2(UPDATA_TYPE,void (*)(UPDATA_PARM *),void (*)(int));
void cpu_reset(void);void clr_update_ram_info(void);
int norflash_origin_read(u8 *,u32,u32);
#endif
