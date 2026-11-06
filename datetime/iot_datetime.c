#include "iot_datetime.h"

/* 软件时钟：无 RTC 时的当前日期时间，可由 iot_datetime_set() 更新 */
static iot_datetime_t s_now;

static int64_t div_floor(int64_t a, int64_t b)
{
    int64_t q = a / b;
    int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) {
        q--;
    }
    return q;
}

/* Howard Hinnant's civil <-> days 算法 */
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
    y -= (m <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? (unsigned)-3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int yy = (int)((int64_t)yoe + era * 400);
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned dd = doy - (153 * mp + 2) / 5 + 1;
    int mm = (int)mp + (mp < 10 ? 3 : -9);

    *y = yy + (mm <= 2 ? 1 : 0);
    *m = (unsigned)mm;
    *d = dd;
}

static void set_weekday(iot_datetime_t *dt)
{
    int64_t days = days_from_civil(dt->year, dt->month, dt->day);
    dt->weekday = (uint8_t)((days % 7 + 7) % 7 + 4) % 7;
}

int iot_datetime_init(void)
{
    s_now.year = 1970;
    s_now.month = 1;
    s_now.day = 1;
    s_now.hour = 0;
    s_now.minute = 0;
    s_now.second = 0;
    s_now.millisecond = 0;
    s_now.weekday = 4; /* 1970-01-01 星期四 */
    return IOT_OK;
}

int iot_datetime_get(iot_datetime_t *dt)
{
    if (dt == NULL) {
        return IOT_ERR_PARAM;
    }
    *dt = s_now;
    return IOT_OK;
}

int iot_datetime_set(const iot_datetime_t *dt)
{
    if (dt == NULL || !iot_datetime_is_valid(dt)) {
        return IOT_ERR_INVALID;
    }
    s_now = *dt;
    set_weekday(&s_now);
    return IOT_OK;
}

bool iot_datetime_is_leap_year(uint16_t year)
{
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

uint8_t iot_datetime_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t dims[] = {31, 28, 31, 30, 31, 30,
                                   31, 31, 30, 31, 30, 31};
    if (month == 0 || month > 12) {
        return 0;
    }
    if (month == 2 && iot_datetime_is_leap_year(year)) {
        return 29;
    }
    return dims[month - 1];
}

bool iot_datetime_is_valid(const iot_datetime_t *dt)
{
    if (dt == NULL) {
        return false;
    }
    if (dt->month == 0 || dt->month > 12) {
        return false;
    }
    if (dt->day == 0 || dt->day > iot_datetime_days_in_month(dt->year, dt->month)) {
        return false;
    }
    if (dt->hour > 23 || dt->minute > 59 || dt->second > 59) {
        return false;
    }
    if (dt->millisecond > 999) {
        return false;
    }
    return true;
}

uint8_t iot_datetime_weekday(uint16_t year, uint8_t month, uint8_t day)
{
    int64_t days = days_from_civil(year, month, day);
    return (uint8_t)(((days % 7 + 7) % 7 + 4) % 7);
}

int iot_datetime_to_timestamp(const iot_datetime_t *dt, int64_t *timestamp)
{
    int64_t days;

    if (dt == NULL || timestamp == NULL || !iot_datetime_is_valid(dt)) {
        return IOT_ERR_INVALID;
    }
    days = days_from_civil(dt->year, dt->month, dt->day);
    *timestamp = days * 86400 + (int64_t)dt->hour * 3600 +
                 (int64_t)dt->minute * 60 + (int64_t)dt->second;
    return IOT_OK;
}

int iot_timestamp_to_datetime(int64_t timestamp, iot_datetime_t *dt)
{
    int64_t days;
    int64_t secs;
    int y;
    unsigned m, d;

    if (dt == NULL) {
        return IOT_ERR_PARAM;
    }

    days = div_floor(timestamp, 86400);
    secs = timestamp - days * 86400;   /* 0..86399 */

    civil_from_days(days, &y, &m, &d);

    dt->year = (uint16_t)y;
    dt->month = (uint8_t)m;
    dt->day = (uint8_t)d;
    dt->hour = (uint8_t)(secs / 3600);
    dt->minute = (uint8_t)((secs % 3600) / 60);
    dt->second = (uint8_t)(secs % 60);
    dt->millisecond = 0;
    set_weekday(dt);
    return IOT_OK;
}

int iot_datetime_compare(const iot_datetime_t *a, const iot_datetime_t *b)
{
    int64_t ta, tb;
    if (a == NULL || b == NULL) {
        return IOT_ERR_PARAM;
    }
    if (iot_datetime_to_timestamp(a, &ta) != IOT_OK ||
        iot_datetime_to_timestamp(b, &tb) != IOT_OK) {
        return IOT_ERR_INVALID;
    }
    if (ta < tb) {
        return -1;
    }
    if (ta > tb) {
        return 1;
    }
    return 0;
}

int iot_datetime_add_seconds(iot_datetime_t *dt, int32_t seconds)
{
    int64_t ts;
    uint16_t ms;

    if (dt == NULL) {
        return IOT_ERR_PARAM;
    }
    ms = dt->millisecond;
    if (iot_datetime_to_timestamp(dt, &ts) != IOT_OK) {
        return IOT_ERR_INVALID;
    }
    ts += seconds;
    if (iot_timestamp_to_datetime(ts, dt) != IOT_OK) {
        return IOT_ERR_INVALID;
    }
    dt->millisecond = ms;
    return IOT_OK;
}