#ifndef SHT2X_H
#define SHT2X_H

/*
 * SHT2x 温湿度传感器驱动（命令式 I2C，地址 0x40/0x41）。
 *
 * 参考 libdriver 设计：
 *   sht2x_init / sht2x_deinit / sht2x_read
 * 所有总线访问经由 drivers/common/driver_io.h。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    bool crc_enabled;
    bool initialized;
} sht2x_handle_t;

/**
 * @brief  初始化 SHT2x（probe + 软复位 + 校验用户寄存器默认值）
 * @param  address  7 位从机地址，0 表示使用默认 0x40
 */
int sht2x_init(sht2x_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

/** @brief 反初始化（不关闭总线） */
int sht2x_deinit(sht2x_handle_t *handle);

/** @brief 软复位并等待 15ms */
int sht2x_soft_reset(sht2x_handle_t *handle);

/**
 * @brief  读取温度与湿度
 * @param  temperature  温度，单位 ℃，可为 NULL
 * @param  humidity     湿度，单位 %RH，可为 NULL
 */
int sht2x_read(sht2x_handle_t *handle, float *temperature, float *humidity);

/** @brief 读取用户寄存器 */
int sht2x_read_user_register(sht2x_handle_t *handle, uint8_t *value);

#ifdef __cplusplus
}
#endif

#endif /* SHT2X_H */
