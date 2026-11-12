#ifndef DS3231_H
#define DS3231_H

/*
 * DS3231 高精度（TCXO 补偿）I2C 实时时钟驱动（7 位地址 0x68）。
 *
 * 除日期时间外提供片内温度读取（±3℃ 精度，0.25℃ 分辨率）。
 * 参考 libdriver 风格：ds3231_init / ds3231_set_time / ds3231_get_time。
 */

#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t year;    /* 2000 - 2199 */
    uint8_t month;    /* 1 - 12 */
    uint8_t day;      /* 1 - 31 */
    uint8_t hour;     /* 0 - 23 */
    uint8_t minute;   /* 0 - 59 */
    uint8_t second;   /* 0 - 59 */
    uint8_t weekday;  /* 1 - 7 */
} ds3231_time_t;

typedef struct {
    hal_i2c_dev_t i2c;
    bool initialized;
} ds3231_handle_t;

/** @brief  初始化并探测从机，address 为 0 时使用 0x68 */
int ds3231_init(ds3231_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

/** @brief  反初始化（不关闭总线） */
int ds3231_deinit(ds3231_handle_t *handle);

/** @brief  写入日期时间 */
int ds3231_set_time(ds3231_handle_t *handle, const ds3231_time_t *time);

/** @brief  读取日期时间 */
int ds3231_get_time(ds3231_handle_t *handle, ds3231_time_t *time);

/** @brief  读取片内温度（℃），必要时强制一次转换 */
int ds3231_get_temperature(ds3231_handle_t *handle, float *celsius);

/**
 * @brief  配置方波输出
 * @param  freq  DS3231_SQW_1HZ / 1024 / 4096 / 8192 / DS3231_SQW_DISABLE
 */
int ds3231_set_square_wave(ds3231_handle_t *handle, uint8_t freq);

/** @brief  清除振荡器停振标志（OSF），返回 1 表示之前曾停振 */
int ds3231_clear_oscillator_flag(ds3231_handle_t *handle, uint8_t *was_stopped);

/** @brief  直接读寄存器 */
int ds3231_read(ds3231_handle_t *handle, uint8_t reg, uint8_t *data, size_t len);

/** @brief  直接写寄存器 */
int ds3231_write(ds3231_handle_t *handle, uint8_t reg, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* DS3231_H */
