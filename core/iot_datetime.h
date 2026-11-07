#ifndef IOT_DATETIME_H
#define IOT_DATETIME_H

/*
 * 时间日期系统（纯 Gregorian 与 Unix 时间戳换算，不依赖硬件 RTC）。
 * weekday: 0 = Sunday, 6 = Saturday。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t year;        /* 年 */
    uint8_t  month;       /* 1..12 */
    uint8_t  day;         /* 1..31 */

    uint8_t  hour;        /* 0..23 */
    uint8_t  minute;      /* 0..59 */
    uint8_t  second;      /* 0..59 */

    uint16_t millisecond; /* 0..999 */

    uint8_t  weekday;     /* 0=周日 .. 6=周六 */
} iot_datetime_t;

int iot_datetime_init(void);

int iot_datetime_get(iot_datetime_t *dt);
int iot_datetime_set(const iot_datetime_t *dt);

int iot_datetime_to_timestamp(const iot_datetime_t *dt, int64_t *timestamp);
int iot_timestamp_to_datetime(int64_t timestamp, iot_datetime_t *dt);

bool iot_datetime_is_valid(const iot_datetime_t *dt);
bool iot_datetime_is_leap_year(uint16_t year);
uint8_t iot_datetime_days_in_month(uint16_t year, uint8_t month);
uint8_t iot_datetime_weekday(uint16_t year, uint8_t month, uint8_t day);

int iot_datetime_compare(const iot_datetime_t *a, const iot_datetime_t *b);
int iot_datetime_add_seconds(iot_datetime_t *dt, int32_t seconds);

#ifdef __cplusplus
}
#endif

#endif /* IOT_DATETIME_H */