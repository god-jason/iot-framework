#ifndef HC_SR501_H
#define HC_SR501_H

/*
 * HC-SR501 人体红外（PIR）传感器驱动。
 *
 * 参考 libdriver 风格：hc_sr501_init / hc_sr501_deinit / hc_sr501_read
 * 引脚配置为下拉输入；上电后请等待 HC_SR501_WARMUP_MS 再采信输出。
 */

#include "common/driver_io.h"
#include "hc_sr501_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    port_pin_t pin;
    bool initialized;
} hc_sr501_handle_t;

/**
 * @brief  初始化：引脚配置为下拉输入
 * @param  pin  输出脚逻辑编号，0 视为无效
 */
int hc_sr501_init(hc_sr501_handle_t *handle, port_pin_t pin);

int hc_sr501_deinit(hc_sr501_handle_t *handle);

/**
 * @brief  读取人体感应状态
 * @param  motion  true 表示检测到移动（按 HC_SR501_ACTIVE_LEVEL 判定）
 */
int hc_sr501_read(hc_sr501_handle_t *handle, bool *motion);

#ifdef __cplusplus
}
#endif

#endif /* HC_SR501_H */
