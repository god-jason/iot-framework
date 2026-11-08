#ifndef SHT35_H
#define SHT35_H

/*
 * SHT35 高精度温湿度传感器驱动（SHT3x 命令集，I2C 0x44/0x45）。
 *
 * 参考 libdriver 设计：
 *   sht35_init / sht35_deinit / sht35_read
 * 所有总线访问经由 drivers/common/driver_io.h。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SHT35_REPEAT_HIGH = 0,
    SHT35_REPEAT_MEDIUM,
    SHT35_REPEAT_LOW
} sht35_repeatability_t;

typedef struct {
    driver_i2c_t i2c;
    sht35_repeatability_t repeatability;
    bool crc_enabled;
    bool initialized;
} sht35_handle_t;

/**
 * @brief  初始化 SHT35（probe + 软复位）
 * @param  address  7 位从机地址，0 表示使用默认 0x44
 */
int sht35_init(sht35_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

/** @brief 反初始化（不关闭总线） */
int sht35_deinit(sht35_handle_t *handle);

/** @brief 软复位并等待 2ms */
int sht35_soft_reset(sht35_handle_t *handle);

/** @brief 设置测量重复性 */
int sht35_set_repeatability(sht35_handle_t *handle, sht35_repeatability_t repeatability);

/** @brief 使能/关闭片上加热器 */
int sht35_set_heater(sht35_handle_t *handle, bool enable);

/**
 * @brief  单次测量并读取温度/湿度
 * @param  temperature  温度，单位 ℃，可为 NULL
 * @param  humidity     湿度，单位 %RH，可为 NULL
 */
int sht35_read(sht35_handle_t *handle, float *temperature, float *humidity);

/** @brief 读取原始值 */
int sht35_read_raw(sht35_handle_t *handle, uint16_t *raw_temperature, uint16_t *raw_humidity);

/** @brief 读取状态寄存器 */
int sht35_read_status(sht35_handle_t *handle, uint16_t *status);

#ifdef __cplusplus
}
#endif

#endif /* SHT35_H */
