/*
 * datetime.h - 日期时间与 Unix 时间戳换算接口
 *
 * 纯软件时钟，按 UTC 处理，不含时区/夏令时；不依赖硬件 RTC。
 * 内部维护一个静态“当前时间”，由 datetime_get/set 访问。
 */

#ifndef DATETIME_H
#define DATETIME_H

/*
 * 时间日期系统（纯 Gregorian 与 Unix 时间戳换算，不依赖硬件 RTC）。
 * weekday: 0 = Sunday, 6 = Saturday。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 日期时间结构体（字段取值范围见注释） */
typedef struct {
    uint16_t year;        /* 年 */
    uint8_t  month;       /* 1..12 */
    uint8_t  day;         /* 1..31，并受当月天数限制 */

    uint8_t  hour;        /* 0..23 */
    uint8_t  minute;      /* 0..59 */
    uint8_t  second;      /* 0..59 */

    uint16_t millisecond; /* 0..999，不参与时间戳换算 */

    uint8_t  weekday;     /* 0=周日 .. 6=周六，由年月日推导 */
} datetime_t;

/* 初始化软件时钟为 1970-01-01 00:00:00；返回 0 */
int datetime_init(void);

/* 读取当前软件时钟；返回 0，-1 表示 dt 为 NULL */
int datetime_get(datetime_t *dt);
/* 设置软件时钟（会按年月日重算 weekday）；返回 0，-1 表示参数非法 */
int datetime_set(const datetime_t *dt);

/* 日期时间 -> Unix 时间戳（UTC 秒）；返回 0，-1 表示参数非法或时间越界 */
int datetime_to_timestamp(const datetime_t *dt, int64_t *timestamp);
/* Unix 时间戳 -> 日期时间（毫秒置 0，自动推导 weekday）；返回 0，-1 表示 dt 为 NULL */
int timestamp_to_datetime(int64_t timestamp, datetime_t *dt);

/* 校验日期时间字段是否合法；返回 true 合法，false 非法 */
bool datetime_is_valid(const datetime_t *dt);
/* 判断是否闰年 */
bool datetime_is_leap_year(uint16_t year);
/* 返回某年某月的天数；月份非法返回 0 */
uint8_t datetime_days_in_month(uint16_t year, uint8_t month);
/* 返回星期：0=周日 .. 6=周六；不校验日期合法性 */
uint8_t datetime_weekday(uint16_t year, uint8_t month, uint8_t day);

/*
 * 比较两个时间：返回 -1（a < b）、0（相等）、1（a > b）；
 * 参数非法或时间不合法时返回 -1，调用前应确保输入有效。
 */
int datetime_compare(const datetime_t *a, const datetime_t *b);
/* 秒级偏移（可为负），会正确处理跨月跨年并保留毫秒；返回 0，-1 表示失败 */
int datetime_add_seconds(datetime_t *dt, int32_t seconds);

#ifdef __cplusplus
}
#endif

#endif /* DATETIME_H */
