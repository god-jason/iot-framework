#ifndef A3144_H
#define A3144_H

/*
 * A3144 霍尔开关（数字量输出）驱动。
 *
 * 参考 libdriver 风格：a3144_init / a3144_deinit / a3144_read
 * 引脚配置为带上拉输入；检测极性见 a3144_register.h 的 A3144_ACTIVE_LEVEL。
 */

#include "common/driver_io.h"
#include "a3144_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    hal_pin_t pin;
    bool initialized;
} a3144_handle_t;

/**
 * @brief  初始化霍尔开关：引脚配置为上拉输入
 * @param  pin  输出脚逻辑编号（HAL_PIN(port, index)），0 视为无效
 */
int a3144_init(a3144_handle_t *handle, hal_pin_t pin);

int a3144_deinit(a3144_handle_t *handle);

/**
 * @brief  读取磁场状态
 * @param  detected  true 表示检测到磁场（按 A3144_ACTIVE_LEVEL 判定）
 */
int a3144_read(a3144_handle_t *handle, bool *detected);

#ifdef __cplusplus
}
#endif

#endif /* A3144_H */
