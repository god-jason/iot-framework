#ifndef PORT_RTC_H
#define PORT_RTC_H

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;

    uint8_t hour;
    uint8_t minute;
    uint8_t second;

    uint8_t weekday;
} port_rtc_time_t;

typedef enum {
    PORT_RTC_ALARM_A = 0,
    PORT_RTC_ALARM_B
} port_rtc_alarm_t;

typedef void (*port_rtc_alarm_callback_t)(port_rtc_alarm_t alarm, void *user_data);

typedef struct {
    port_rtc_time_t time;

    port_rtc_alarm_callback_t callback;
    void *user_data;
} port_rtc_alarm_config_t;

port_status_t port_rtc_init(void);
port_status_t port_rtc_deinit(void);

port_status_t port_rtc_set_time(const port_rtc_time_t *time);
port_status_t port_rtc_get_time(port_rtc_time_t *time);

port_status_t port_rtc_set_alarm(port_rtc_alarm_t alarm, const port_rtc_alarm_config_t *config);
port_status_t port_rtc_disable_alarm(port_rtc_alarm_t alarm);

port_status_t port_rtc_get_timestamp(uint32_t *timestamp);

#ifdef __cplusplus
}
#endif

#endif /* PORT_RTC_H */
