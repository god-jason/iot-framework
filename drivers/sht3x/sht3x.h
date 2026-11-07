#ifndef SHT3X_H
#define SHT3X_H

/*
 * SHT3x 温湿度传感器驱动（Sensirion，命令式 I2C，地址 0x44/0x45）。
 *
 * 参考 libdriver 风格：
 *   sht3x_init / sht3x_deinit / sht3x_read / sht3x_read_raw
 *
 * 实现说明：与 drivers/sht30 使用同一套命令集与 CRC 算法，为了不复制两份
 * CRC 逻辑，本驱动是对 sht30 的薄封装（句柄内嵌 sht30_handle_t），
 * 硬件访问仍只经由 drivers/common/driver_io.h。
 *
 * 输出单位：温度 ℃，相对湿度 %RH。
 *
 * 依赖 sht30 的行为语义（编译器无法校验，修改 drivers/sht30 时请同步核对）：
 *   1. address == 0 时回落到 0x44（备用地址 0x45 需显式传入）；
 *   2. sht30_read() 返回的已是 ℃ 与 %RH，且湿度已钳位到 0~100，
 *      换算为 T = -45 + 175 * raw / 65535、RH = 100 * raw / 65535；
 *   3. sht30_read_raw() 固定等待 SHT30_MEASURE_DELAY_MS（20ms）后读取，
 *      该等待不随 repeatability 变化，repeatability 只影响下发的测量命令。
 * 地址/测量命令/CRC 参数另有 sht3x_register.h 中的编译期断言兜底。
 */

#include "common/driver_io.h"
#include "sht30/sht30.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    sht30_handle_t dev;     /* 复用的 SHT30 上下文（含 CRC 使能标志） */
} sht3x_handle_t;

/**
 * @brief  初始化 SHT3x（probe + 软复位）
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位从机地址，0 表示默认 0x44（备用地址 0x45）
 * @return 0 成功，负值为 iot_status_t
 */
int sht3x_init(sht3x_handle_t *handle, iot_i2c_t *i2c, uint16_t address);

/** @brief 反初始化（不关闭总线，不下发命令） */
int sht3x_deinit(sht3x_handle_t *handle);

/**
 * @brief  单次测量温度与相对湿度（高重复性，内部等待转换并校验 CRC）
 * @param  temperature  温度输出，单位 ℃，可为 NULL
 * @param  humidity     湿度输出，单位 %RH，可为 NULL
 */
int sht3x_read(sht3x_handle_t *handle, float *temperature, float *humidity);

/**
 * @brief  读取原始 16 位 ADC 值（便于上层自行标定）
 * @param  raw_temperature  可为 NULL
 * @param  raw_humidity     可为 NULL
 */
int sht3x_read_raw(sht3x_handle_t *handle, uint16_t *raw_temperature, uint16_t *raw_humidity);

/** @brief 软复位并等待 2ms */
int sht3x_soft_reset(sht3x_handle_t *handle);

/** @brief 使能/关闭片上加热器 */
int sht3x_set_heater(sht3x_handle_t *handle, bool enable);

#ifdef __cplusplus
}
#endif

#endif /* SHT3X_H */
