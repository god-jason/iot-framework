#ifndef AHT20_H
#define AHT20_H

/*
 * AHT20 / DHT20 温湿度传感器驱动。
 *
 * 参考 libdriver 设计：
 *   - aht20_init / aht20_deinit / aht20_read 为公开 API
 *   - 所有总线访问经由 hal 的 I2C 抽象
 *
 * 用法：
 *   aht20_handle_t aht20;
 *   aht20_init(&aht20, &i2c1, AHT20_DEFAULT_ADDRESS);
 *   float t, h;
 *   aht20_read(&aht20, &t, &h);
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    bool initialized;
} aht20_handle_t;

/**
 * @brief  初始化 AHT20
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位从机地址，通常为 0x38
 * @return 0 成功，负值为 hal_status_t
 */
int aht20_init(aht20_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

/**
 * @brief  反初始化（不关闭总线）
 */
int aht20_deinit(aht20_handle_t *handle);

/**
 * @brief  启动一次测量并读取结果（内部完成 80ms 等待）
 * @param  temperature  温度，单位 ℃，可为 NULL
 * @param  humidity     湿度，单位 %RH，可为 NULL
 */
int aht20_read(aht20_handle_t *handle, float *temperature, float *humidity);

/**
 * @brief  读取状态字节
 */
int aht20_get_status(aht20_handle_t *handle, uint8_t *status);

#ifdef __cplusplus
}
#endif

#endif /* AHT20_H */
