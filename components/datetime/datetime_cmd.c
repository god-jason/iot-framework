/*
 * datetime_cmd.c - datetime 组件 roshell 命令（date）
 *
 * 读写内核 ros_rtc 的系统时间（硬件 RTC 优先，否则 1Hz 软件计时），
 * 并用 datetime 模块完成时间戳换算。
 */

#include "datetime_cmd.h"

#include "datetime.h"
#include "ros.h"
#include "roshell.h"
#include "ros_rtc.h"
#include "common.h"

static const char *const s_weekday_name[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

/* 打印系统时间，并同步模块内的软件时钟 */
static void cmd_show(uint32_t ts)
{
    datetime_t dt;
    const char *src;
    const char *wd;

    if (timestamp_to_datetime((int64_t)ts, &dt) != 0) {
        return;
    }
    (void)datetime_set(&dt);

    wd = (dt.weekday < 7U) ? s_weekday_name[dt.weekday] : "?";
    switch (ros_rtc_source()) {
    case ROS_RTC_SOURCE_HW:   src = "hw";   break;
    case ROS_RTC_SOURCE_TICK: src = "tick"; break;
    default:                  src = "none"; break;
    }
    ros_printf("%04u-%02u-%02u %02u:%02u:%02u %s unix=%lu src=%s\n",
               (unsigned)dt.year, (unsigned)dt.month, (unsigned)dt.day,
               (unsigned)dt.hour, (unsigned)dt.minute, (unsigned)dt.second,
               wd, (unsigned long)ts, src);
}

static int date_cmd_handler(roshell_t *rs, int argc, char **argv,
                            const rs_args_parsed_t *parsed, void *user)
{
    (void)rs;
    (void)argc;
    (void)argv;
    (void)user;
    const char *action = NULL;
    const char *v1 = NULL;
    uint32_t ts;
    int rc;

    (void)rs_args_get_str(parsed, 0U, &action);
    (void)rs_args_get_str(parsed, 1U, &v1);

    if (action == NULL || str_icmp(action, "get")) {
        cmd_show(ros_rtc_get_timestamp());
        return 0;
    }
    if (!str_icmp(action, "set") || v1 == NULL) {
        ros_puts("usage: date [get] | date set <unix>");
        return -1;
    }

    if (!str_parse_uint(v1, &ts)) {
        ros_printf("ERR: invalid unix time '%s'\n", v1);
        return -1;
    }

    rc = ros_rtc_set_timestamp(ts);
    if (rc != ROS_OK) {
        ros_printf("ERR: rtc backend unavailable (%d)\n", rc);
        return -1;
    }
    cmd_show(ts);
    return 0;
}

static const rs_args_t s_date_args[] = {
    { "action", 0, RS_ARGS_TYPE_STR, true, false, NULL, "get|set" },
    { "value", 0, RS_ARGS_TYPE_STR, true, false, NULL, "unix seconds" }
};

static const rs_cmd_t s_date_def = {
    "date", "get / set system time",
    "date [get] | date set <unix>",
    NULL, s_date_args, 2U, date_cmd_handler, NULL, NULL, NULL, NULL
};

int datetime_cmd_register(void)
{
    static bool registered;

    if (registered) {
        return 0;
    }
    if (rs_cmd_register(&s_date_def) != 0) {
        return -1;
    }
    registered = true;
    return 0;
}
