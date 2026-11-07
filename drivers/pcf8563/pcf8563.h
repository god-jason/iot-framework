#ifndef PCF8563_H
#define PCF8563_H

/*
 * PCF8563 I2C 实时时钟 / 日历驱动（7 位地址 0x51）。
 *
 * 分辨率 1 秒，BCD 编码寄存器，支持世纪位与 CLKOUT 方波输出。
 * 参考 libdriver 风格：pcf8563_init / pcf8563_set_time / pcf8563_get_time。
 */

#include "common/driver_io.h"

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
    uint8_t weekday;  /* 0 - 6 */
} pcf8563_time_t;

typedef struct {
    driver_i2c_t i2c;
    bool initialized;
} pcf8563_handle_t;

/** @brief  初始化并探测从机，address 为 0 时使用 0x51 */
int pcf8563_init(pcf8563_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

/** @brief  反初始化（不关闭总线） */
int pcf8563_deinit(pcf8563_handle_t *handle);

/** @brief  写入日期时间（清除 STOP 位，启动计数） */
int pcf8563_set_time(pcf8563_handle_t *handle, const pcf8563_time_t *time);

/** @brief  读取日期时间；VL 置位时返回 PORT_NOT_READY 提示时间可能不可信 */
int pcf8563_get_time(pcf8563_handle_t *handle, pcf8563_time_t *time);

/**
 * @brief  配置 CLKOUT 输出
 * @param  freq  PCF8563_CLKOUT_32768HZ / 1024 / 32 / 1；0xFF 关闭输出
 */
int pcf8563_set_clkout(pcf8563_handle_t *handle, uint8_t freq);

/** @brief  直接读寄存器 */
int pcf8563_read(pcf8563_handle_t *handle, uint8_t reg, uint8_t *data, size_t len);

/** @brief  直接写寄存器 */
int pcf8563_write(pcf8563_handle_t *handle, uint8_t reg, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* PCF8563_H */
