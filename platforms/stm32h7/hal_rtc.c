/*
 * hal_rtc.c - STM32H7 RTC HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 */

#if defined(ROS_STM32_H7)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

static RTC_HandleTypeDef s_stm32_rtc;
static bool s_stm32_rtc_ready;
static hal_rtc_alarm_callback_t s_stm32_rtc_alarm_cb[2];
static void *s_stm32_rtc_alarm_user[2];

static int64_t stm32_days_from_civil(int32_t year, uint32_t month, uint32_t day)
{
    int32_t era;
    int32_t adjusted_month;
    uint32_t year_of_era;
    uint32_t day_of_year;
    uint32_t day_of_era;

    year -= (month <= 2U) ? 1 : 0;
    era = (year >= 0 ? year : year - 399) / 400;
    year_of_era = (uint32_t)(year - era * 400);
    adjusted_month = (int32_t)month + ((month > 2U) ? -3 : 9);
    day_of_year = (uint32_t)((153 * adjusted_month + 2) / 5) + day - 1U;
    day_of_era = year_of_era * 365U + year_of_era / 4U -
                 year_of_era / 100U + day_of_year;
    return (int64_t)era * 146097 + (int64_t)day_of_era - 719468;
}

int hal_rtc_init(void)
{
    if (s_stm32_rtc_ready) {
        return HAL_OK;
    }

    memset(&s_stm32_rtc, 0, sizeof(s_stm32_rtc));
    s_stm32_rtc.Instance = RTC;
    s_stm32_rtc.Init.HourFormat = RTC_HOURFORMAT_24;
    s_stm32_rtc.Init.AsynchPrediv = 0x7FU;
    s_stm32_rtc.Init.SynchPrediv = 0xFFU;
    s_stm32_rtc.Init.OutPut = RTC_OUTPUT_DISABLE;
    s_stm32_rtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    s_stm32_rtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;

    if (HAL_RTC_Init(&s_stm32_rtc) != HAL_OK) {
        return HAL_ERROR;
    }

    s_stm32_rtc_ready = true;
    return HAL_OK;
}

int hal_rtc_deinit(void)
{
    if (!s_stm32_rtc_ready) {
        return HAL_NOT_READY;
    }
    (void)HAL_RTC_DeInit(&s_stm32_rtc);
    s_stm32_rtc_ready = false;
    s_stm32_rtc_alarm_cb[0] = NULL;
    s_stm32_rtc_alarm_cb[1] = NULL;
    s_stm32_rtc_alarm_user[0] = NULL;
    s_stm32_rtc_alarm_user[1] = NULL;
    return HAL_OK;
}

int hal_rtc_set_time(const hal_rtc_time_t *time)
{
    RTC_TimeTypeDef rtc_time;
    RTC_DateTypeDef rtc_date;

    if (time == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (!s_stm32_rtc_ready) {
        return HAL_NOT_READY;
    }

    memset(&rtc_time, 0, sizeof(rtc_time));
    rtc_time.Hours = time->hour;
    rtc_time.Minutes = time->minute;
    rtc_time.Seconds = time->second;

    if (HAL_RTC_SetTime(&s_stm32_rtc, &rtc_time, RTC_FORMAT_BIN) != HAL_OK) {
        return HAL_ERROR;
    }

    memset(&rtc_date, 0, sizeof(rtc_date));
    rtc_date.WeekDay = (time->weekday >= 1U && time->weekday <= 7U) ?
                       time->weekday : 1U;
    rtc_date.Month = time->month;
    rtc_date.Date = time->day;
    rtc_date.Year = (uint8_t)(time->year % 100U);

    if (HAL_RTC_SetDate(&s_stm32_rtc, &rtc_date, RTC_FORMAT_BIN) != HAL_OK) {
        return HAL_ERROR;
    }
    return HAL_OK;
}

int hal_rtc_get_time(hal_rtc_time_t *time)
{
    RTC_TimeTypeDef rtc_time;
    RTC_DateTypeDef rtc_date;

    if (time == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (!s_stm32_rtc_ready) {
        return HAL_NOT_READY;
    }

    memset(&rtc_time, 0, sizeof(rtc_time));
    memset(&rtc_date, 0, sizeof(rtc_date));
    if (HAL_RTC_GetTime(&s_stm32_rtc, &rtc_time, RTC_FORMAT_BIN) != HAL_OK) {
        return HAL_ERROR;
    }
    if (HAL_RTC_GetDate(&s_stm32_rtc, &rtc_date, RTC_FORMAT_BIN) != HAL_OK) {
        return HAL_ERROR;
    }

    time->year = (uint16_t)(2000U + rtc_date.Year);
    time->month = rtc_date.Month;
    time->day = rtc_date.Date;
    time->weekday = rtc_date.WeekDay;
    time->hour = rtc_time.Hours;
    time->minute = rtc_time.Minutes;
    time->second = rtc_time.Seconds;
    return HAL_OK;
}

int hal_rtc_set_alarm(hal_rtc_alarm_e alarm,
                                 const hal_rtc_alarm_config_t *config)
{
    RTC_AlarmTypeDef rtc_alarm;

    if (config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (!s_stm32_rtc_ready) {
        return HAL_NOT_READY;
    }
    if ((alarm != HAL_RTC_ALARM_A) && (alarm != HAL_RTC_ALARM_B)) {
        return HAL_INVALID_PARAM;
    }

    memset(&rtc_alarm, 0, sizeof(rtc_alarm));
    rtc_alarm.AlarmTime.Hours = config->time.hour;
    rtc_alarm.AlarmTime.Minutes = config->time.minute;
    rtc_alarm.AlarmTime.Seconds = config->time.second;
    rtc_alarm.AlarmMask = RTC_ALARMMASK_NONE;
    rtc_alarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;
    rtc_alarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
    rtc_alarm.AlarmDateWeekDay = 1U;
    rtc_alarm.Alarm = (alarm == HAL_RTC_ALARM_A) ?
                      RTC_ALARM_A : RTC_ALARM_B;

    if (HAL_RTC_SetAlarm_IT(&s_stm32_rtc, &rtc_alarm, RTC_FORMAT_BIN) != HAL_OK) {
        return HAL_ERROR;
    }

    s_stm32_rtc_alarm_cb[(alarm == HAL_RTC_ALARM_A) ? 0 : 1] = config->callback;
    s_stm32_rtc_alarm_user[(alarm == HAL_RTC_ALARM_A) ? 0 : 1] = config->user_data;
    return HAL_OK;
}

int hal_rtc_disable_alarm(hal_rtc_alarm_e alarm)
{
    if (!s_stm32_rtc_ready) {
        return HAL_NOT_READY;
    }
    if ((alarm != HAL_RTC_ALARM_A) && (alarm != HAL_RTC_ALARM_B)) {
        return HAL_INVALID_PARAM;
    }

    s_stm32_rtc_alarm_cb[(alarm == HAL_RTC_ALARM_A) ? 0 : 1] = NULL;
    s_stm32_rtc_alarm_user[(alarm == HAL_RTC_ALARM_A) ? 0 : 1] = NULL;
    return ros_cube_status(HAL_RTC_DeactivateAlarm(
        &s_stm32_rtc, (alarm == HAL_RTC_ALARM_A) ? RTC_ALARM_A : RTC_ALARM_B));
}

int hal_rtc_get_timestamp(uint32_t *timestamp)
{
    hal_rtc_time_t time;
    int64_t days;
    int64_t seconds;

    if (timestamp == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (hal_rtc_get_time(&time) != HAL_OK) {
        return HAL_NOT_READY;
    }

    days = stm32_days_from_civil((int32_t)time.year, time.month, time.day);
    seconds = days * 86400 + (int64_t)time.hour * 3600 +
              (int64_t)time.minute * 60 + time.second;
    *timestamp = (uint32_t)seconds;
    return HAL_OK;
}

void ros_stm32_rtc_alarm_irq_handler(void)
{
    bool alarm_a;
    bool alarm_b;

    if (!s_stm32_rtc_ready) {
        return;
    }

    alarm_a = (__HAL_RTC_ALARM_GET_FLAG(&s_stm32_rtc, RTC_FLAG_ALRAF) != RESET);
    alarm_b = (__HAL_RTC_ALARM_GET_FLAG(&s_stm32_rtc, RTC_FLAG_ALRBF) != RESET);
    HAL_RTC_AlarmIRQHandler(&s_stm32_rtc);

    if (alarm_a && (s_stm32_rtc_alarm_cb[0] != NULL)) {
        s_stm32_rtc_alarm_cb[0](HAL_RTC_ALARM_A, s_stm32_rtc_alarm_user[0]);
    }
    if (alarm_b && (s_stm32_rtc_alarm_cb[1] != NULL)) {
        s_stm32_rtc_alarm_cb[1](HAL_RTC_ALARM_B, s_stm32_rtc_alarm_user[1]);
    }
}


#endif /* ROS_STM32_H7 */
