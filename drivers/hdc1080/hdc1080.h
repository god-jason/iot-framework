#ifndef HDC1080_H
#define HDC1080_H

/*
 * HDC1080 温湿度传感器驱动（I2C，地址 0x40）。
 *
 * 参考 libdriver 设计：
 *   hdc1080_init / hdc1080_deinit / hdc1080_read
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
} hdc1080_handle_t;

/**
 * @brief  初始化 HDC1080（probe + 校验器件 ID 0x1050，配置为 14 位双通道）
 * @param  address  7 位从机地址，0 表示使用默认 0x40
 */
int hdc1080_init(hdc1080_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

/** @brief 反初始化（不关闭总线） */
int hdc1080_deinit(hdc1080_handle_t *handle);

/**
 * @brief  读取温度与湿度
 * @param  temperature  温度，单位 ℃，可为 NULL
 * @param  humidity     湿度，单位 %RH，可为 NULL
 */
int hdc1080_read(hdc1080_handle_t *handle, float *temperature, float *humidity);

/** @brief 读取器件 ID（正常应为 0x1050） */
int hdc1080_read_id(hdc1080_handle_t *handle, uint16_t *id);

/** @brief 使能/关闭片上加热器 */
int hdc1080_set_heater(hdc1080_handle_t *handle, bool enable);

#ifdef __cplusplus
}
#endif

#endif /* HDC1080_H */
