#ifndef ADS1110_H
#define ADS1110_H

/*
 * ADS1110 16 位单通道 Δ-Σ ADC 驱动（I2C，内置 2.048V 基准与 PGA）。
 *
 * 参考 libdriver 风格：ads1110_init / ads1110_read / ads1110_set_gain ...
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 可编程增益（对应配置字节 SC[1:0]） */
typedef enum {
    ADS1110_GAIN_1 = 0,
    ADS1110_GAIN_2,
    ADS1110_GAIN_4,
    ADS1110_GAIN_8
} ads1110_gain_t;

/* 数据速率（对应配置字节 DR[1:0]） */
typedef enum {
    ADS1110_RATE_15SPS = 0,
    ADS1110_RATE_30SPS,
    ADS1110_RATE_60SPS,
    ADS1110_RATE_240SPS
} ads1110_rate_t;

typedef struct {
    driver_i2c_t i2c;
    ads1110_gain_t gain;
    ads1110_rate_t data_rate;
    bool initialized;
} ads1110_handle_t;

/**
 * @brief  初始化：探测器件并校验配置字节保留位
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位地址（0x48~0x4F），传 0 使用 0x48
 * @return 0 成功，负值为 port_status_t
 */
int ads1110_init(ads1110_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

/** @brief 反初始化（不关闭总线） */
int ads1110_deinit(ads1110_handle_t *handle);

/** @brief 设置增益（写配置寄存器） */
int ads1110_set_gain(ads1110_handle_t *handle, ads1110_gain_t gain);

/** @brief 设置数据速率（写配置寄存器） */
int ads1110_set_data_rate(ads1110_handle_t *handle, ads1110_rate_t data_rate);

/** @brief 仅启动一次转换，由调用方自行延时后读取 */
int ads1110_start(ads1110_handle_t *handle);

/**
 * @brief  启动转换、按速率等待并读取结果
 * @param  raw  有符号 16 位原始码值
 */
int ads1110_read(ads1110_handle_t *handle, int16_t *raw);

/** @brief 读取换算后的电压（V，按当前增益满量程计算） */
int ads1110_read_voltage(ads1110_handle_t *handle, float *volts);

/** @brief 读回配置字节 */
int ads1110_get_config(ads1110_handle_t *handle, uint8_t *config);

#ifdef __cplusplus
}
#endif

#endif /* ADS1110_H */
