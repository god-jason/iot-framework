#ifndef HTU31D_H
#define HTU31D_H

/*
 * HTU31D 温湿度传感器驱动（命令式 I2C，地址 0x40/0x41）。
 *
 * 参考 libdriver 设计：
 *   htu31d_init / htu31d_deinit / htu31d_read
 * 所有总线访问经由 drivers/common/driver_io.h。
 *
 * 用法：
 *   htu31d_handle_t htu;
 *   htu31d_init(&htu, &i2c1, HTU31D_DEFAULT_ADDRESS);
 *   float t, rh;
 *   htu31d_read(&htu, &t, &rh);
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    bool crc_enabled;
    bool initialized;
} htu31d_handle_t;

/**
 * @brief  初始化 HTU31D（probe + 校验厂商 ID）
 * @param  address  7 位从机地址，0 表示使用默认 0x40
 */
int htu31d_init(htu31d_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

/** @brief 反初始化（不关闭总线） */
int htu31d_deinit(htu31d_handle_t *handle);

/**
 * @brief  读取温度与湿度
 * @param  temperature  温度，单位 ℃，可为 NULL
 * @param  humidity     湿度，单位 %RH，可为 NULL
 */
int htu31d_read(htu31d_handle_t *handle, float *temperature, float *humidity);

/** @brief 读取厂商 ID（正常应为 0x0008） */
int htu31d_read_manufacturer_id(htu31d_handle_t *handle, uint16_t *id);

#ifdef __cplusplus
}
#endif

#endif /* HTU31D_H */
