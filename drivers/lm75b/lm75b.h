#ifndef LM75B_H
#define LM75B_H

/*
 * LM75B 数字温度传感器驱动（I2C，7 位地址 0x48 - 0x4F）。
 *
 * 11 位分辨率（0.125 ℃），测量范围 -55 - +125 ℃，
 * 内置 TOS / THYST 阈值、关断模式与故障队列。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    bool initialized;
} lm75b_handle_t;

/** @brief  初始化并探测从机，address 为 0 时使用 0x48 */
int lm75b_init(lm75b_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

/** @brief  反初始化（不关闭总线） */
int lm75b_deinit(lm75b_handle_t *handle);

/** @brief  读取温度（℃） */
int lm75b_read(lm75b_handle_t *handle, float *celsius);

/** @brief  读取温度原始码（左对齐 11 位，1 LSB = 0.125 ℃） */
int lm75b_read_raw(lm75b_handle_t *handle, int16_t *raw);

/** @brief  读取配置寄存器 */
int lm75b_get_config(lm75b_handle_t *handle, uint8_t *config);

/** @brief  写配置寄存器 */
int lm75b_set_config(lm75b_handle_t *handle, uint8_t config);

/** @brief  设置过温阈值 TOS（℃） */
int lm75b_set_overtemperature(lm75b_handle_t *handle, float celsius);

/** @brief  设置迟滞阈值 THYST（℃） */
int lm75b_set_hysteresis(lm75b_handle_t *handle, float celsius);

#ifdef __cplusplus
}
#endif

#endif /* LM75B_H */
