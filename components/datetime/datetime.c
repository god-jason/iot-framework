/*
 * datetime.c - 日期时间与 Unix 时间戳换算（纯软件时钟）
 *
 * 模块职责：
 *   提供公历日期时间结构体与 Unix 时间戳（UTC，秒）之间的双向换算、
 *   合法性校验、闰年/月天数查询、星期计算与秒级加减。
 *   模块内维护一个软件“当前时间”（s_now），无 RTC 时作为系统时间来源。
 *
 * 数据流与依赖：
 *   仅依赖 标准头文件，无平台接口与动态内存。
 *   历法换算采用 Howard Hinnant 的 civil_from_days / days_from_civil 算法，
 *   以 1970-01-01 为纪元，按 400 年 = 146097 天的周期处理闰年规则。
 *
 * 边界约束：
 *   - 时间戳为 int64_t 秒，负值表示 1970 年之前，换算用整除向下取整保证正确。
 *   - 年份以 uint16_t 保存；时间戳换算结果超出范围时会被截断（未做检查）。
 *   - 所有换算按 UTC 处理，不包含时区与夏令时。
 *   - 静态变量 s_now 非线程安全，多任务访问需调用者加锁。
 */

#include "datetime.h"

/* 软件时钟：无 RTC 时的当前日期时间，可由 datetime_set() 更新 */
static datetime_t s_now;

/*
 * 整数向下取整除法（C99 的 '/' 向零取整，对负数会产生偏差）。
 * 参数：a - 被除数；b - 除数（不可为 0）
 * 返回：floor(a / b)
 * 注意：用于把负时间戳拆分为“天数 + 当日秒数”，保证余数落在 [0, 86399]。
 */
static int64_t div_floor(int64_t a, int64_t b)
{
    int64_t q = a / b;
    int64_t r = a % b;
    /* 余数与除数异号时，商需要再减 1 才是向下取整 */
    if (r != 0 && ((r < 0) != (b < 0))) {
        q--;
    }
    return q;
}

/*
 * 公历日期 -> 距 1970-01-01 的天数（Howard Hinnant 算法）。
 * 参数：y - 年；m - 月（1..12）；d - 日（1..31）
 * 返回：天数，1970-01-01 为 0，之前为负
 * 注意：内部把 3 月当作一年的起点，使闰日位于年末，便于按 400 年周期计算；
 *       调用者需保证月/日合法（本函数不做校验）。
 */
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
    y -= (m <= 2);   /* 1、2 月归入上一年，保证闰日在年末 */
    int64_t era = (y >= 0 ? y : y - 399) / 400;   /* 400 年周期序号 */
    unsigned yoe = (unsigned)(y - era * 400);     /* 周期内的年 0..399 */
    /* 年内第几天：m > 2 时取 m - 3（以无符号回绕实现减法），否则 m + 9 */
    unsigned doy = (153 * (m + (m > 2 ? (unsigned)-3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;  /* 周期内天数 */
    return era * 146097 + (int64_t)doe - 719468;  /* 719468 = 1970-01-01 的纪元偏移 */
}

/*
 * 距 1970-01-01 的天数 -> 公历日期（Howard Hinnant 算法的逆运算）。
 * 参数：z - 天数（可为负）；y/m/d - 输出年月日，均不可为 NULL
 * 返回：无
 * 注意：与 days_from_civil() 使用同一套 400 年周期假设，二者互为逆运算。
 */
static void civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d)
{
    z += 719468;   /* 平移到算法内部纪元 */
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);   /* 周期内天数 0..146096 */
    /* 从周期内天数反推年序号，扣除 4/100/400 年闰日影响 */
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int yy = (int)((int64_t)yoe + era * 400);
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);   /* 年内第几天 */
    unsigned mp = (5 * doy + 2) / 153;   /* 以 3 月为起点的月序号 0..11 */
    unsigned dd = doy - (153 * mp + 2) / 5 + 1;
    int mm = (int)mp + (mp < 10 ? 3 : -9);   /* 还原为 1..12 月 */

    *y = yy + (mm <= 2 ? 1 : 0);   /* 1、2 月属于下一年 */
    *m = (unsigned)mm;
    *d = dd;
}

/*
 * 根据年月日重新计算并写入 weekday。
 * 参数：dt - 日期时间（使用其中的 year/month/day 字段）
 * 返回：无
 * 注意：1970-01-01 为星期四（4），故星期 = (天数 + 4) mod 7；
 *       先 %7 再加 7 再取模，保证负天数也能得到 0..6。
 */
static void set_weekday(datetime_t *dt)
{
    int64_t days = days_from_civil(dt->year, dt->month, dt->day);
    dt->weekday = (uint8_t)((days % 7 + 7) % 7 + 4) % 7;
}

/*
 * 初始化软件时钟为 1970-01-01 00:00:00（星期四）。
 * 参数：无
 * 返回：0
 * 注意：会覆盖之前通过 datetime_set() 设置的时间。
 */
int datetime_init(void)
{
    s_now.year = 1970;
    s_now.month = 1;
    s_now.day = 1;
    s_now.hour = 0;
    s_now.minute = 0;
    s_now.second = 0;
    s_now.millisecond = 0;
    s_now.weekday = 4; /* 1970-01-01 星期四 */
    return 0;
}

/*
 * 获取当前软件时钟。
 * 参数：dt - 输出缓冲，不可为 NULL
 * 返回：0 成功；-1 参数非法
 * 注意：整结构体拷贝，包含 weekday 字段。
 */
int datetime_get(datetime_t *dt)
{
    if (dt == NULL) {
        return -1;
    }
    *dt = s_now;
    return 0;
}

/*
 * 设置当前软件时钟。
 * 参数：dt - 新时间，不可为 NULL，且必须通过合法性校验
 * 返回：0 成功；-1 参数非法或日期时间越界
 * 注意：weekday 由年月日重新推导，调用者传入的 weekday 会被覆盖。
 */
int datetime_set(const datetime_t *dt)
{
    if (dt == NULL || !datetime_is_valid(dt)) {
        return -1;
    }
    s_now = *dt;
    set_weekday(&s_now);
    return 0;
}

/*
 * 判断是否闰年（公历规则：能被 4 整除且不能被 100 整除，或能被 400 整除）。
 * 参数：year - 年份
 * 返回：true 闰年；false 平年
 */
bool datetime_is_leap_year(uint16_t year)
{
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

/*
 * 查询某年某月的天数。
 * 参数：year - 年份；month - 月份（1..12）
 * 返回：当月天数；month 非法（0 或 > 12）时返回 0
 * 注意：2 月按是否闰年返回 28 或 29。
 */
uint8_t datetime_days_in_month(uint16_t year, uint8_t month)
{
    /* 平年各月天数（下标 0 对应 1 月） */
    static const uint8_t dims[] = {31, 28, 31, 30, 31, 30,
                                   31, 31, 30, 31, 30, 31};
    if (month == 0 || month > 12) {
        return 0;
    }
    if (month == 2 && datetime_is_leap_year(year)) {
        return 29;
    }
    return dims[month - 1];
}

/*
 * 校验日期时间各字段是否在合法范围内。
 * 参数：dt - 待校验结构体，可为 NULL
 * 返回：true 合法；false 非法（NULL、月份越界、日超出当月天数、
 *       时分秒或毫秒越界）
 * 注意：weekday 字段不参与校验；年份无范围限制。
 */
bool datetime_is_valid(const datetime_t *dt)
{
    if (dt == NULL) {
        return false;
    }
    if (dt->month == 0 || dt->month > 12) {
        return false;
    }
    if (dt->day == 0 || dt->day > datetime_days_in_month(dt->year, dt->month)) {
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

/*
 * 计算某日期是星期几。
 * 参数：year - 年；month - 月（1..12）；day - 日
 * 返回：0=周日，1=周一 ... 6=周六
 * 注意：与 set_weekday() 使用相同公式；不校验日期合法性。
 */
uint8_t datetime_weekday(uint16_t year, uint8_t month, uint8_t day)
{
    int64_t days = days_from_civil(year, month, day);
    /* 1970-01-01 是周四(4)，加 7 取模规避负天数 */
    return (uint8_t)(((days % 7 + 7) % 7 + 4) % 7);
}

/*
 * 日期时间 -> Unix 时间戳（UTC 秒）。
 * 参数：dt - 输入日期时间（必须合法）；timestamp - 输出时间戳
 * 返回：0 成功；-1 参数非法或日期时间越界
 * 注意：毫秒字段不参与换算，直接丢弃。
 */
int datetime_to_timestamp(const datetime_t *dt, int64_t *timestamp)
{
    int64_t days;

    if (dt == NULL || timestamp == NULL || !datetime_is_valid(dt)) {
        return -1;
    }
    days = days_from_civil(dt->year, dt->month, dt->day);
    *timestamp = days * 86400 + (int64_t)dt->hour * 3600 +
                 (int64_t)dt->minute * 60 + (int64_t)dt->second;
    return 0;
}

/*
 * Unix 时间戳（UTC 秒）-> 日期时间。
 * 参数：timestamp - 输入时间戳（可为负）；dt - 输出结构体
 * 返回：0 成功；-1 dt 为 NULL
 * 注意：输出的毫秒恒为 0，weekday 自动推导；
 *       年份超出 uint16_t 范围时会被截断。
 */
int timestamp_to_datetime(int64_t timestamp, datetime_t *dt)
{
    int64_t days;
    int64_t secs;
    int y;
    unsigned m, d;

    if (dt == NULL) {
        return -1;
    }

    /* 拆分为天数与当日秒数：div_floor 保证 secs 非负 */
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
    return 0;
}

/*
 * 比较两个日期时间的先后。
 * 参数：a/b - 待比较时间，均不可为 NULL
 * 返回：-1 表示 a < b；0 表示相等；1 表示 a > b；
 *       参数非法或任一时间不合法时返回 -1（无法与“a < b”区分）
 * 注意：比较基于秒级时间戳，忽略毫秒字段。
 */
int datetime_compare(const datetime_t *a, const datetime_t *b)
{
    int64_t ta, tb;
    if (a == NULL || b == NULL) {
        return -1;
    }
    if (datetime_to_timestamp(a, &ta) != 0 ||
        datetime_to_timestamp(b, &tb) != 0) {
        return -1;
    }
    if (ta < tb) {
        return -1;
    }
    if (ta > tb) {
        return 1;
    }
    return 0;
}

/*
 * 在日期时间上增加（或减少）若干秒。
 * 参数：dt - 输入输出时间；seconds - 偏移量，可为负
 * 返回：0 成功；-1 参数非法或时间不合法
 * 注意：换算经由时间戳完成，因此会正确跨月、跨年；
 *       原毫秒字段在换算后恢复；weekday 自动更新。
 */
int datetime_add_seconds(datetime_t *dt, int32_t seconds)
{
    int64_t ts;
    uint16_t ms;

    if (dt == NULL) {
        return -1;
    }
    ms = dt->millisecond;   /* 时间戳换算不保留毫秒，先暂存 */
    if (datetime_to_timestamp(dt, &ts) != 0) {
        return -1;
    }
    ts += seconds;
    if (timestamp_to_datetime(ts, dt) != 0) {
        return -1;
    }
    dt->millisecond = ms;
    return 0;
}
