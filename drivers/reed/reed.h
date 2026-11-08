#ifndef REED_H
#define REED_H

/*
 * 干簧管（磁簧开关）驱动。
 *
 * 参考 libdriver 风格：reed_init / reed_deinit / reed_read
 * 引脚配置为上拉输入；闭合极性见 reed_register.h 的 REED_ACTIVE_LEVEL。
 */

#include "common/driver_io.h"
#include "reed_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    hal_pin_t pin;
    bool initialized;
} reed_handle_t;

/**
 * @brief  初始化：引脚配置为上拉输入
 * @param  pin  输出脚逻辑编号，0 视为无效
 */
int reed_init(reed_handle_t *handle, hal_pin_t pin);

int reed_deinit(reed_handle_t *handle);

/**
 * @brief  读取触点状态
 * @param  closed  true 表示磁铁靠近、触点闭合
 */
int reed_read(reed_handle_t *handle, bool *closed);

#ifdef __cplusplus
}
#endif

#endif /* REED_H */
