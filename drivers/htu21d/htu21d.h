#ifndef HTU21D_H
#define HTU21D_H

/*
 * HTU21D 温湿度传感器驱动（命令式 I2C，地址 0x40）。
 *
 * 参考 libdriver 设计：
 *   htu21d_init / htu21d_deinit / htu21d_read
 * 所有总线访问经由 drivers/common/driver_io.h，不依赖任何 MCU HAL。
 *
 * 用法：
 *   htu21d_handle_t htu;
 *   htu21d_init(&htu, &i2c1, HTU21D_DEFAULT_ADDRESS);
 *   float t, rh;
 *   htu21d_read(&htu, &t, &rh);
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    bool crc_enabled;
    bool initialized;
} htu21d_handle_t;

/**
 * @brief  初始化 HTU21D
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位从机地址，通常 0x40（0 表示使用默认值）
 * @return 0 成功，负值为 HAL 状态码
 */
int htu21d_init(htu21d_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

/** @brief 反初始化（不关闭总线） */
int htu21d_deinit(htu21d_handle_t *handle);

/** @brief 软复位并等待 15ms */
int htu21d_soft_reset(htu21d_handle_t *handle);

/**
 * @brief  读取温度与湿度（内部完成两次转换及 CRC 校验）
 * @param  temperature  温度，单位 ℃，可为 NULL
 * @param  humidity     湿度，单位 %RH，可为 NULL
 */
int htu21d_read(htu21d_handle_t *handle, float *temperature, float *humidity);

/** @brief 读取用户寄存器（可用于确认器件在位） */
int htu21d_read_user_register(htu21d_handle_t *handle, uint8_t *value);

#ifdef __cplusplus
}
#endif

#endif /* HTU21D_H */
