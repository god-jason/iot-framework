#ifndef MAX30205_H
#define MAX30205_H

/*
 * MAX30205 人体体温传感器驱动（I2C，7 位地址 0x48 - 0x4B）。
 *
 * 16 位有符号温度寄存器，分辨率 0.00390625 ℃，测量范围 0 - 50 ℃，
 * 典型精度 ±0.1 ℃（37 ℃）。内置 TOS / THYST 阈值比较与关断模式。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    bool initialized;
} max30205_handle_t;

/** @brief  初始化并探测从机，address 为 0 时使用 0x48 */
int max30205_init(max30205_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

/** @brief  反初始化（不关闭总线） */
int max30205_deinit(max30205_handle_t *handle);

/** @brief  读取温度（℃） */
int max30205_read(max30205_handle_t *handle, float *celsius);

/** @brief  读取原始 16 位温度码（1 LSB = 1/256 ℃） */
int max30205_read_raw(max30205_handle_t *handle, int16_t *raw);

/** @brief  读取配置寄存器 */
int max30205_get_config(max30205_handle_t *handle, uint16_t *config);

/** @brief  写配置寄存器 */
int max30205_set_config(max30205_handle_t *handle, uint16_t config);

/** @brief  设置过温阈值 TOS（℃） */
int max30205_set_overtemperature(max30205_handle_t *handle, float celsius);

/** @brief  设置迟滞阈值 THYST（℃） */
int max30205_set_hysteresis(max30205_handle_t *handle, float celsius);

#ifdef __cplusplus
}
#endif

#endif /* MAX30205_H */
