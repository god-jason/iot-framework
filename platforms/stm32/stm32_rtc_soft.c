/*
 * stm32_rtc_soft.c - STM32 1Hz 软件 RTC 后端（F1/F4 共用）
 *
 * 未启用硬件 RTC 或 LSE 未起振时使用：以 ROS_RTC_DEFAULT_UNIX 为基准，
 * 由内核 tick 分频出 1Hz 秒计数，分辨率 1 秒，不占用额外定时器或中断。
 */

#include "ros.h"
#include "stm32.h"

#include <stddef.h>

static uint32_t s_base_unix = ROS_RTC_DEFAULT_UNIX;
static uint32_t s_base_tick;

static void soft_base_set(uint32_t timestamp, uint32_t tick)
{
    ros_port_critical_enter();
    s_base_unix = timestamp;
    s_base_tick = tick;
    ros_port_critical_exit();
}

static int soft_get(uint32_t *timestamp)
{
    uint32_t base;
    uint32_t tick;

    if (timestamp == NULL) {
        return ROS_INVALID_PARAM;
    }
    ros_port_critical_enter();
    base = s_base_unix;
    tick = s_base_tick;
    ros_port_critical_exit();

    *timestamp = base + (ros_tick_get() - tick) / ROS_TICK_HZ;
    return ROS_OK;
}

static int soft_set(uint32_t timestamp)
{
    soft_base_set(timestamp, ros_tick_get());
    return ROS_OK;
}

static const ros_rtc_ops_t s_rtc_ops = {
    .name = "soft-tick",
    .source = ROS_RTC_SOURCE_TICK,
    .get = soft_get,
    .set = soft_set
};

void stm32_rtc_soft_init(void)
{
    soft_base_set((uint32_t)ROS_RTC_DEFAULT_UNIX, ros_tick_get());
    (void)ros_rtc_register(&s_rtc_ops);
}
