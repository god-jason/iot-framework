#ifndef HAL_RTC_H
#define HAL_RTC_H

/*
 * 实时时钟：日期时间读写、闹钟配置与禁用、Unix 时间戳获取。
 * RTC 为全局单实例资源，因此接口不接收句柄。
 *
 * 时间字段采用公历表示，weekday 由调用者按平台约定填写（通常 1~7）。
 * 返回值未特别说明时：HAL_OK 成功，HAL_INVALID_PARAM 参数为空，
 * HAL_NOT_READY 表示 RTC 未初始化。
 */

#include "hal_def.h"

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
} hal_rtc_time_t;

/* 闹钟通道 */
typedef enum {
    HAL_RTC_ALARM_A = 0,   /* 闹钟 A */
    HAL_RTC_ALARM_B        /* 闹钟 B */
} hal_rtc_alarm_t;

/*
 * 闹钟回调。
 * alarm     : 触发的闹钟通道；
 * user_data : 注册时绑定的上下文，可为 NULL。
 * 注意：执行于中断上下文，禁止阻塞。
 */
typedef void (*hal_rtc_alarm_callback_t)(hal_rtc_alarm_t alarm, void *user_data);

/* 闹钟配置 */
typedef struct {
    hal_rtc_time_t time;                   /* 闹钟触发时间，仅比较已设置的字段（平台实现决定） */

    hal_rtc_alarm_callback_t callback;     /* 闹钟回调，可为 NULL */
    void *user_data;                        /* 回调上下文，可为 NULL */
} hal_rtc_alarm_config_t;

/*
 * 初始化 RTC。
 * 返回 : HAL_OK；时钟源不可用等错误由平台返回。
 */
hal_status_t hal_rtc_init(void);

/* 反初始化 RTC，关闭闹钟；返回 HAL_OK / HAL_NOT_READY */
hal_status_t hal_rtc_deinit(void);

/*
 * 设置当前日期时间。
 * time : 时间结构，不可为 NULL；字段越界返回 HAL_INVALID_PARAM。
 */
hal_status_t hal_rtc_set_time(const hal_rtc_time_t *time);

/*
 * 读取当前日期时间。
 * time : 输出结构，不可为 NULL。
 */
hal_status_t hal_rtc_get_time(hal_rtc_time_t *time);

/*
 * 设置并启用闹钟。
 * alarm  : 闹钟通道；
 * config : 配置，不可为 NULL。
 * 返回   : HAL_OK；HAL_INVALID_PARAM；通道不存在返回 HAL_NOT_SUPPORTED。
 */
hal_status_t hal_rtc_set_alarm(hal_rtc_alarm_t alarm, const hal_rtc_alarm_config_t *config);

/* 禁用指定闹钟通道；返回 HAL_OK / HAL_INVALID_PARAM */
hal_status_t hal_rtc_disable_alarm(hal_rtc_alarm_t alarm);

/*
 * 获取 Unix 时间戳（自 1970-01-01 00:00:00 UTC 起的秒数）。
 * timestamp : 输出，不可为 NULL。
 * 注意      : 32 位时间戳在 2038 年后溢出，平台可返回 HAL_OVERFLOW。
 */
hal_status_t hal_rtc_get_timestamp(uint32_t *timestamp);

#ifdef __cplusplus
}
#endif

#endif /* HAL_RTC_H */
