#ifndef SHT31_H
#define SHT31_H

/*
 * SHT31 温湿度传感器驱动（SHT3x 命令集，I2C 地址 0x44/0x45）。
 *
 * 参考 libdriver 设计：
 *   sht31_init / sht31_deinit / sht31_read
 * 所有总线访问经由 drivers/common/driver_io.h。
 *
 * 用法：
 *   sht31_handle_t sht;
 *   sht31_init(&sht, &i2c1, SHT31_DEFAULT_ADDRESS);
 *   float t, rh;
 *   sht31_read(&sht, &t, &rh);
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SHT31_REPEAT_HIGH = 0,
    SHT31_REPEAT_MEDIUM,
    SHT31_REPEAT_LOW
} sht31_repeatability_t;

typedef struct {
    driver_i2c_t i2c;
    sht31_repeatability_t repeatability;
    bool crc_enabled;
    bool initialized;
} sht31_handle_t;

/**
 * @brief  初始化 SHT31（probe + 软复位）
 * @param  address  7 位从机地址，0 表示使用默认 0x44
 */
int sht31_init(sht31_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

/** @brief 反初始化（不关闭总线） */
int sht31_deinit(sht31_handle_t *handle);

/** @brief 软复位并等待 2ms */
int sht31_soft_reset(sht31_handle_t *handle);

/** @brief 设置测量重复性 */
int sht31_set_repeatability(sht31_handle_t *handle, sht31_repeatability_t repeatability);

/** @brief 使能/关闭片上加热器 */
int sht31_set_heater(sht31_handle_t *handle, bool enable);

/**
 * @brief  单次测量并读取温度/湿度（内部完成转换等待与 CRC 校验）
 * @param  temperature  温度，单位 ℃，可为 NULL
 * @param  humidity     湿度，单位 %RH，可为 NULL
 */
int sht31_read(sht31_handle_t *handle, float *temperature, float *humidity);

/** @brief 读取原始值（便于上层标定） */
int sht31_read_raw(sht31_handle_t *handle, uint16_t *raw_temperature, uint16_t *raw_humidity);

/** @brief 读取状态寄存器 */
int sht31_read_status(sht31_handle_t *handle, uint16_t *status);

#ifdef __cplusplus
}
#endif

#endif /* SHT31_H */
