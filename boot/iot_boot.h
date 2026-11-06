#ifndef IOT_BOOT_H
#define IOT_BOOT_H

/*
 * Bootloader 接口（声明）。具体实现属平台相关：
 * Flash 写入、向量表切换、栈指针设置与跳转等不能伪装成完全通用的 C 实现。
 * 本文件仅定义统一接口，供 Bootloader 工程实现，APP 侧调用 request/confirm。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IOT_BOOT_NORMAL,
    IOT_BOOT_UPGRADE,
    IOT_BOOT_RECOVERY,
    IOT_BOOT_ROLLBACK
} iot_boot_mode_t;

/* 平台跳转钩子：向量表切换 + 跳转 APP 入口，由具体 MCU 实现 */
typedef void (*iot_boot_jump_fn)(void);
int iot_boot_set_jump(iot_boot_jump_fn jump);

int iot_boot_init(void);
iot_boot_mode_t iot_boot_get_mode(void);

int iot_boot_select_image(void);
int iot_boot_verify_image(void);
int iot_boot_install_image(void);
int iot_boot_start_application(void);

int iot_boot_request_upgrade(void);
int iot_boot_request_rollback(void);
int iot_boot_confirm_application(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_BOOT_H */