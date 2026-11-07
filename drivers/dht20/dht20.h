#ifndef DHT20_H
#define DHT20_H

/*
 * DHT20 温湿度传感器驱动（I2C 0x38，协议兼容 AHT20）。
 *
 * 参考 libdriver 设计：
 *   dht20_init / dht20_deinit / dht20_read
 * 所有总线访问经由 drivers/common/driver_io.h。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    bool crc_enabled;    /* 初始化时试读判定模组是否提供 CRC */
    bool initialized;
} dht20_handle_t;

/**
 * @brief  初始化 DHT20（上电延时 + 软复位 + 必要校准）
 * @param  address  7 位从机地址，0 表示使用默认 0x38
 */
int dht20_init(dht20_handle_t *handle, iot_i2c_t *i2c, uint16_t address);

/** @brief 反初始化（不关闭总线） */
int dht20_deinit(dht20_handle_t *handle);

/**
 * @brief  触发测量并读取温度/湿度
 * @param  temperature  温度，单位 ℃，可为 NULL
 * @param  humidity     湿度，单位 %RH，可为 NULL
 */
int dht20_read(dht20_handle_t *handle, float *temperature, float *humidity);

/** @brief 读取状态字节 */
int dht20_get_status(dht20_handle_t *handle, uint8_t *status);

#ifdef __cplusplus
}
#endif

#endif /* DHT20_H */
