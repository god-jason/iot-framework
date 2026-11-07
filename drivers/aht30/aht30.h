#ifndef AHT30_H
#define AHT30_H

/*
 * AHT30 温湿度传感器驱动（I2C 0x38，协议兼容 AHT20）。
 *
 * 参考 libdriver 设计：
 *   aht30_init / aht30_deinit / aht30_read
 * 所有总线访问经由 drivers/common/driver_io.h。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    bool initialized;
} aht30_handle_t;

/**
 * @brief  初始化 AHT30（上电延时 + 软复位 + 必要校准）
 * @param  address  7 位从机地址，0 表示使用默认 0x38
 */
int aht30_init(aht30_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

/** @brief 反初始化（不关闭总线） */
int aht30_deinit(aht30_handle_t *handle);

/**
 * @brief  触发测量并读取温度/湿度（内部完成 80ms 等待）
 * @param  temperature  温度，单位 ℃，可为 NULL
 * @param  humidity     湿度，单位 %RH，可为 NULL
 */
int aht30_read(aht30_handle_t *handle, float *temperature, float *humidity);

/** @brief 读取状态字节 */
int aht30_get_status(aht30_handle_t *handle, uint8_t *status);

#ifdef __cplusplus
}
#endif

#endif /* AHT30_H */
