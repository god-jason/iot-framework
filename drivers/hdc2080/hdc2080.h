#ifndef HDC2080_H
#define HDC2080_H

/*
 * HDC2080 温湿度传感器驱动（I2C，地址 0x41）。
 *
 * 参考 libdriver 设计：
 *   hdc2080_init / hdc2080_deinit / hdc2080_read
 * 所有总线访问经由 drivers/common/driver_io.h。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    bool heater_enabled;
    bool initialized;
} hdc2080_handle_t;

/**
 * @brief  初始化 HDC2080（probe + 校验器件 ID 0x07D0）
 * @param  address  7 位从机地址，0 表示使用默认 0x41
 */
int hdc2080_init(hdc2080_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

/** @brief 反初始化（不关闭总线） */
int hdc2080_deinit(hdc2080_handle_t *handle);

/**
 * @brief  触发一次测量并读取温度/湿度
 * @param  temperature  温度，单位 ℃，可为 NULL
 * @param  humidity     湿度，单位 %RH，可为 NULL
 */
int hdc2080_read(hdc2080_handle_t *handle, float *temperature, float *humidity);

/** @brief 读取器件 ID（正常应为 0x07D0） */
int hdc2080_read_id(hdc2080_handle_t *handle, uint16_t *id);

#ifdef __cplusplus
}
#endif

#endif /* HDC2080_H */
