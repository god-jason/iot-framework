#ifndef SHT4X_H
#define SHT4X_H

/*
 * SHT4x 温湿度传感器驱动（命令式 I2C，地址 0x44/0x45）。
 *
 * 参考 libdriver 设计：
 *   sht4x_init / sht4x_deinit / sht4x_read
 * 所有总线访问经由 drivers/common/driver_io.h。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SHT4X_REPEAT_HIGH = 0,
    SHT4X_REPEAT_MEDIUM,
    SHT4X_REPEAT_LOW
} sht4x_repeatability_e;

typedef struct {
    driver_i2c_t i2c;
    sht4x_repeatability_e repeatability;
    bool crc_enabled;
    bool initialized;
} sht4x_handle_t;

/**
 * @brief  初始化 SHT4x（probe + 读序列号确认器件在位）
 * @param  address  7 位从机地址，0 表示使用默认 0x44
 */
int sht4x_init(sht4x_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

/** @brief 反初始化（不关闭总线） */
int sht4x_deinit(sht4x_handle_t *handle);

/** @brief 软复位并等待 2ms */
int sht4x_soft_reset(sht4x_handle_t *handle);

/** @brief 设置测量重复性（影响转换时间与噪声） */
int sht4x_set_repeatability(sht4x_handle_t *handle, sht4x_repeatability_e repeatability);

/**
 * @brief  单次测量并读取温度/湿度
 * @param  temperature  温度，单位 ℃，可为 NULL
 * @param  humidity     湿度，单位 %RH，可为 NULL
 */
int sht4x_read(sht4x_handle_t *handle, float *temperature, float *humidity);

/** @brief 读取 32 位序列号（拆为 2 字节 + CRC，两次转换） */
int sht4x_read_serial(sht4x_handle_t *handle, uint16_t *serial);

#ifdef __cplusplus
}
#endif

#endif /* SHT4X_H */
