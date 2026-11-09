#ifndef ADS1118_H
#define ADS1118_H

/*
 * ADS1118 16 位 4 通道 Δ-Σ ADC 驱动（SPI，模式 1：CPOL=0 / CPHA=1）。
 *
 * 支持 4 路单端 / 4 组差分输入与内部温度传感器。
 * 参考 libdriver 风格：ads1118_init / ads1118_read / ads1118_set_pga ...
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ADS1118_PGA_6V144 = 0,  /* ±6.144V */
    ADS1118_PGA_4V096,      /* ±4.096V */
    ADS1118_PGA_2V048,      /* ±2.048V（复位默认） */
    ADS1118_PGA_1V024,      /* ±1.024V */
    ADS1118_PGA_0V512,      /* ±0.512V */
    ADS1118_PGA_0V256       /* ±0.256V */
} ads1118_pga_e;

typedef enum {
    ADS1118_DR_8 = 0,
    ADS1118_DR_16,
    ADS1118_DR_32,
    ADS1118_DR_64,
    ADS1118_DR_128,         /* 复位默认 */
    ADS1118_DR_250,
    ADS1118_DR_475,
    ADS1118_DR_860
} ads1118_dr_e;

typedef enum {
    ADS1118_MUX_DIFF_0_1 = 0,   /* AIN0 - AIN1 */
    ADS1118_MUX_DIFF_0_3,       /* AIN0 - AIN3 */
    ADS1118_MUX_DIFF_1_3,       /* AIN1 - AIN3 */
    ADS1118_MUX_DIFF_2_3,       /* AIN2 - AIN3 */
    ADS1118_MUX_SINGLE_0,       /* AIN0 - GND */
    ADS1118_MUX_SINGLE_1,
    ADS1118_MUX_SINGLE_2,
    ADS1118_MUX_SINGLE_3
} ads1118_mux_e;

typedef struct {
    driver_spi_t spi;
    ads1118_pga_e pga;
    ads1118_dr_e data_rate;
    bool initialized;
} ads1118_handle_t;

/**
 * @brief  初始化：配置片选、回读配置寄存器校验保留位
 * @param  handle  驱动句柄
 * @param  spi     已初始化的 SPI 主机句柄（模式 1，8 位，MSB first）
 * @param  cs      片选引脚，HAL_PIN(0,0) 表示由总线管理
 * @return 0 成功，负值为 HAL 状态码（无响应返回 HAL_NOT_FOUND）
 */
int ads1118_init(ads1118_handle_t *handle, hal_spi_t *spi, hal_pin_t cs);

/** @brief 反初始化（拉高片选，不关闭总线） */
int ads1118_deinit(ads1118_handle_t *handle);

/** @brief 设置 PGA */
int ads1118_set_pga(ads1118_handle_t *handle, ads1118_pga_e pga);

/** @brief 设置数据速率 */
int ads1118_set_data_rate(ads1118_handle_t *handle, ads1118_dr_e data_rate);

/**
 * @brief  对指定 MUX 做一次单次转换
 * @param  mux  输入选择
 * @param  raw  原始有符号 16 位码值
 */
int ads1118_read_raw(ads1118_handle_t *handle, ads1118_mux_e mux, int16_t *raw);

/** @brief 读取指定 MUX 的电压（V） */
int ads1118_read_voltage(ads1118_handle_t *handle, ads1118_mux_e mux, float *volts);

/** @brief 读取内部温度传感器（℃） */
int ads1118_read_internal_temp(ads1118_handle_t *handle, float *celsius);

/**
 * @brief  连续读取 count 个单端通道原始值
 * @param  values  调用方提供的缓冲区
 * @param  count   1~4
 */
int ads1118_read(ads1118_handle_t *handle, int16_t *values, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* ADS1118_H */
