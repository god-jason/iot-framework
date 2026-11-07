#ifndef ADS1115_H
#define ADS1115_H

/*
 * ADS1115 16 位 4 通道 Δ-Σ ADC 驱动（I2C）。
 *
 * 支持 4 路单端 / 4 组差分输入，单次转换模式（内部轮询 OS 位等待完成）。
 * 参考 libdriver 风格：ads1115_init / ads1115_read / ads1115_set_pga ...
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 可编程增益，括号内为对应满量程 */
typedef enum {
    ADS1115_PGA_6V144 = 0,  /* ±6.144V */
    ADS1115_PGA_4V096,      /* ±4.096V */
    ADS1115_PGA_2V048,      /* ±2.048V（复位默认） */
    ADS1115_PGA_1V024,      /* ±1.024V */
    ADS1115_PGA_0V512,      /* ±0.512V */
    ADS1115_PGA_0V256       /* ±0.256V */
} ads1115_pga_t;

/* 数据速率（SPS） */
typedef enum {
    ADS1115_DR_8 = 0,
    ADS1115_DR_16,
    ADS1115_DR_32,
    ADS1115_DR_64,
    ADS1115_DR_128,         /* 复位默认 */
    ADS1115_DR_250,
    ADS1115_DR_475,
    ADS1115_DR_860
} ads1115_dr_t;

/* 输入通道选择（对应 MUX[2:0]） */
typedef enum {
    ADS1115_MUX_DIFF_0_1 = 0,   /* AIN0 - AIN1 */
    ADS1115_MUX_DIFF_0_3,       /* AIN0 - AIN3 */
    ADS1115_MUX_DIFF_1_3,       /* AIN1 - AIN3 */
    ADS1115_MUX_DIFF_2_3,       /* AIN2 - AIN3 */
    ADS1115_MUX_SINGLE_0,       /* AIN0 - GND */
    ADS1115_MUX_SINGLE_1,
    ADS1115_MUX_SINGLE_2,
    ADS1115_MUX_SINGLE_3
} ads1115_mux_t;

typedef struct {
    driver_i2c_t i2c;
    ads1115_pga_t pga;
    ads1115_dr_t data_rate;
    bool initialized;
} ads1115_handle_t;

/**
 * @brief  初始化：探测器件、关闭比较器、写入默认配置并回读校验
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位地址（0x48~0x4B），传 0 使用 0x48
 * @return 0 成功，负值为 iot_status_t（无应答返回 IOT_NOT_FOUND）
 */
int ads1115_init(ads1115_handle_t *handle, iot_i2c_t *i2c, uint16_t address);

/** @brief 反初始化（不关闭总线） */
int ads1115_deinit(ads1115_handle_t *handle);

/** @brief 设置 PGA */
int ads1115_set_pga(ads1115_handle_t *handle, ads1115_pga_t pga);

/** @brief 设置数据速率 */
int ads1115_set_data_rate(ads1115_handle_t *handle, ads1115_dr_t data_rate);

/**
 * @brief  对指定 MUX 做一次单次转换
 * @param  mux  输入选择（单端或差分）
 * @param  raw  原始有符号 16 位码值
 */
int ads1115_read_raw(ads1115_handle_t *handle, ads1115_mux_t mux, int16_t *raw);

/**
 * @brief  读取单端通道电压
 * @param  channel  0~3
 * @param  volts    电压（V），按当前 PGA 满量程换算
 */
int ads1115_read_voltage(ads1115_handle_t *handle, uint8_t channel, float *volts);

/**
 * @brief  连续读取 count 个单端通道原始值
 * @param  values  调用方提供的缓冲区
 * @param  count   1~4
 */
int ads1115_read(ads1115_handle_t *handle, int16_t *values, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* ADS1115_H */
