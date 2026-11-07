#ifndef IOT_RTC_H
#define IOT_RTC_H

/*
 * 实时时钟：日期时间读写、闹钟配置与禁用、Unix 时间戳获取。
 * RTC 为全局单实例资源，因此接口不接收句柄。
 *
 * 时间字段采用公历表示，weekday 由调用者按平台约定填写（通常 1~7）。
 * 返回值未特别说明时：IOT_OK 成功，IOT_INVALID_PARAM 参数为空，
 * IOT_NOT_READY 表示 RTC 未初始化。
 */

#include "iot_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 日期时间 */
typedef struct {
    uint16_t year;      /* 年，如 2026 */
    uint8_t month;      /* 月，1~12 */
    uint8_t day;        /* 日，1~31 */

    uint8_t hour;       /* 时，0~23 */
    uint8_t minute;     /* 分，0~59 */
    uint8_t second;     /* 秒，0~59 */

    uint8_t weekday;    /* 星期，取值含义由平台约定 */
} iot_rtc_time_t;

/* 闹钟通道 */
typedef enum {
    IOT_RTC_ALARM_A = 0,   /* 闹钟 A */
    IOT_RTC_ALARM_B        /* 闹钟 B */
} iot_rtc_alarm_t;

/*
 * 闹钟回调。
 * alarm     : 触发的闹钟通道；
 * user_data : 注册时绑定的上下文，可为 NULL。
 * 注意：执行于中断上下文，禁止阻塞。
 */
typedef void (*iot_rtc_alarm_callback_t)(iot_rtc_alarm_t alarm, void *user_data);

/* 闹钟配置 */
typedef struct {
    iot_rtc_time_t time;                   /* 闹钟触发时间，仅比较已设置的字段（平台实现决定） */

    iot_rtc_alarm_callback_t callback;     /* 闹钟回调，可为 NULL */
    void *user_data;                        /* 回调上下文，可为 NULL */
} iot_rtc_alarm_config_t;

/*
 * 初始化 RTC。
 * 返回 : IOT_OK；时钟源不可用等错误由平台返回。
 */
iot_status_t iot_rtc_init(void);

/* 反初始化 RTC，关闭闹钟；返回 IOT_OK / IOT_NOT_READY */
iot_status_t iot_rtc_deinit(void);

/*
 * 设置当前日期时间。
 * time : 时间结构，不可为 NULL；字段越界返回 IOT_INVALID_PARAM。
 */
iot_status_t iot_rtc_set_time(const iot_rtc_time_t *time);

/*
 * 读取当前日期时间。
 * time : 输出结构，不可为 NULL。
 */
iot_status_t iot_rtc_get_time(iot_rtc_time_t *time);

/*
 * 设置并启用闹钟。
 * alarm  : 闹钟通道；
 * config : 配置，不可为 NULL。
 * 返回   : IOT_OK；IOT_INVALID_PARAM；通道不存在返回 IOT_NOT_SUPPORTED。
 */
iot_status_t iot_rtc_set_alarm(iot_rtc_alarm_t alarm, const iot_rtc_alarm_config_t *config);

/* 禁用指定闹钟通道；返回 IOT_OK / IOT_INVALID_PARAM */
iot_status_t iot_rtc_disable_alarm(iot_rtc_alarm_t alarm);

/*
 * 获取 Unix 时间戳（自 1970-01-01 00:00:00 UTC 起的秒数）。
 * timestamp : 输出，不可为 NULL。
 * 注意      : 32 位时间戳在 2038 年后溢出，平台可返回 IOT_OVERFLOW。
 */
iot_status_t iot_rtc_get_timestamp(uint32_t *timestamp);

#ifdef __cplusplus
}
#endif

#endif /* IOT_RTC_H */
