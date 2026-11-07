#ifndef DS1307_H
#define DS1307_H

/*
 * DS1307 I2C 实时时钟驱动（7 位地址 0x68）。
 *
 * 提供 BCD 转换后的日期时间读写、方波输出设置与 56 字节 NV SRAM 访问。
 * 参考 libdriver 风格：ds1307_init / ds1307_set_time / ds1307_get_time。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t year;    /* 2000 - 2099 */
    uint8_t month;    /* 1 - 12 */
    uint8_t day;      /* 1 - 31 */
    uint8_t hour;     /* 0 - 23 */
    uint8_t minute;   /* 0 - 59 */
    uint8_t second;   /* 0 - 59 */
    uint8_t weekday;  /* 1 - 7 */
} ds1307_time_t;

typedef struct {
    driver_i2c_t i2c;
    bool initialized;
} ds1307_handle_t;

/**
 * @brief  初始化并探测从机
 * @param  address  7 位地址，0 表示使用 0x68
 * @return 0 成功，IOT_NOT_FOUND 表示无应答
 */
int ds1307_init(ds1307_handle_t *handle, iot_i2c_t *i2c, uint16_t address);

/** @brief  反初始化（不关闭总线） */
int ds1307_deinit(ds1307_handle_t *handle);

/** @brief  写入日期时间（同时清除 CH 位，启动振荡器） */
int ds1307_set_time(ds1307_handle_t *handle, const ds1307_time_t *time);

/** @brief  读取日期时间 */
int ds1307_get_time(ds1307_handle_t *handle, ds1307_time_t *time);

/**
 * @brief  设置方波输出
 * @param  rate  DS1307_SQW_1HZ / 4096 / 8192 / 32768 / DS1307_SQW_DISABLE
 */
int ds1307_set_square_wave(ds1307_handle_t *handle, uint8_t rate);

/** @brief  读取 NV SRAM（offset 0 - 55） */
int ds1307_read_ram(ds1307_handle_t *handle, uint8_t offset, uint8_t *data, size_t len);

/** @brief  写入 NV SRAM（offset 0 - 55） */
int ds1307_write_ram(ds1307_handle_t *handle, uint8_t offset, const uint8_t *data, size_t len);

/** @brief  直接读寄存器 */
int ds1307_read(ds1307_handle_t *handle, uint8_t reg, uint8_t *data, size_t len);

/** @brief  直接写寄存器 */
int ds1307_write(ds1307_handle_t *handle, uint8_t reg, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* DS1307_H */
