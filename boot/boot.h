#ifndef BOOT_H
#define BOOT_H

/*
 * Bootloader 接口（声明）。具体实现属平台相关：
 * Flash 写入、向量表切换、栈指针设置与跳转等不能伪装成完全通用的 C 实现。
 * 本文件仅定义统一接口，供 Bootloader 工程实现，APP 侧调用 request/confirm。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOOT_NORMAL,
    BOOT_UPGRADE,
    BOOT_RECOVERY,
    BOOT_ROLLBACK
} boot_mode_t;

/* 平台跳转钩子：向量表切换 + 跳转 APP 入口，由具体 MCU 实现 */
typedef void (*boot_jump_fn)(void);
int boot_set_jump(boot_jump_fn jump);

int boot_init(void);
boot_mode_t boot_get_mode(void);

int boot_select_image(void);
int boot_verify_image(void);
int boot_install_image(void);
int boot_start_application(void);

int boot_request_upgrade(void);
int boot_request_rollback(void);
int boot_confirm_application(void);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_H */