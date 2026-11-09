#ifndef DS1302_H
#define DS1302_H

/*
 * DS1302 涓流充电实时时钟驱动（3 线同步串行：CE / SCLK / IO）。
 *
 * 三线时序由 GPIO 软件模拟，IO 为双向单线（写时输出、读时输入）。
 * 参考 libdriver 风格：ds1302_init / ds1302_set_time / ds1302_get_time。
 *
 * 用法：
 *   ds1302_handle_t rtc;
 *   ds1302_init(&rtc, HAL_PIN(1, 0), HAL_PIN(1, 1), HAL_PIN(1, 2));
 *   ds1302_time_t t = { 2026, 11, 7, 12, 0, 0, 6 };
 *   ds1302_set_time(&rtc, &t);
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 日期时间（BCD 转换由驱动完成） */
typedef struct {
    uint16_t year;    /* 2000 - 2099 */
    uint8_t month;    /* 1 - 12 */
    uint8_t day;      /* 1 - 31 */
    uint8_t hour;     /* 0 - 23 */
    uint8_t minute;   /* 0 - 59 */
    uint8_t second;   /* 0 - 59 */
    uint8_t weekday;  /* 1 - 7，调用方自定义映射（通常 1=周日） */
} ds1302_time_t;

typedef struct {
    hal_pin_t ce;    /* 片选 / 使能 */
    hal_pin_t sclk;  /* 串行时钟 */
    hal_pin_t io;    /* 双向数据线 */
    bool initialized;
} ds1302_handle_t;

/**
 * @brief  初始化三线 GPIO 并解除写保护
 * @return 0 成功，负值为 HAL 状态码
 */
int ds1302_init(ds1302_handle_t *handle, hal_pin_t ce, hal_pin_t sclk, hal_pin_t io);

/** @brief  反初始化（释放三个引脚） */
int ds1302_deinit(ds1302_handle_t *handle);

/** @brief  写入日期时间（自动清除写保护、启动振荡器） */
int ds1302_set_time(ds1302_handle_t *handle, const ds1302_time_t *time);

/** @brief  读取日期时间（清除 CH 位影响，BCD 转二进制） */
int ds1302_get_time(ds1302_handle_t *handle, ds1302_time_t *time);

/** @brief  写保护开关 */
int ds1302_set_write_protect(ds1302_handle_t *handle, bool enable);

/**
 * @brief  设置涓流充电
 * @param  config  DS1302_TRICKLE_xxx 之一，DS1302_TRICKLE_DISABLE 关闭
 */
int ds1302_set_trickle_charger(ds1302_handle_t *handle, uint8_t config);

/**
 * @brief  读取内部 RAM（31 字节）
 * @param  offset  0 - 30
 * @param  data    调用方缓冲区
 * @param  len     读取长度，offset + len 不得超过 31
 */
int ds1302_read_ram(ds1302_handle_t *handle, uint8_t offset, uint8_t *data, size_t len);

/** @brief  写入内部 RAM，参数约束同 ds1302_read_ram */
int ds1302_write_ram(ds1302_handle_t *handle, uint8_t offset, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* DS1302_H */
