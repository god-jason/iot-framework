#ifndef AM312_H
#define AM312_H

/*
 * AM312 人体红外（PIR）传感器驱动。
 *
 * 参考 libdriver 风格：am312_init / am312_deinit / am312_read
 * 引脚配置为下拉输入；上电后请等待 AM312_WARMUP_MS 再采信输出。
 */

#include "common/driver_io.h"
#include "am312_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    hal_pin_t pin;
    bool initialized;
} am312_handle_t;

/**
 * @brief  初始化：引脚配置为下拉输入
 * @param  pin  输出脚逻辑编号，0 视为无效
 */
int am312_init(am312_handle_t *handle, hal_pin_t pin);

int am312_deinit(am312_handle_t *handle);

/**
 * @brief  读取人体感应状态
 * @param  motion  true 表示检测到移动（按 AM312_ACTIVE_LEVEL 判定）
 */
int am312_read(am312_handle_t *handle, bool *motion);

#ifdef __cplusplus
}
#endif

#endif /* AM312_H */
