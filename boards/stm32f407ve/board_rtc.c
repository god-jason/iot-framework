/*
 * board_rtc.c - STM32F407VE 板级 RTC 后端（1Hz 软件计时）
 *
 * 本板未使用硬件 RTC，以 ROS_RTC_DEFAULT_UNIX 为基准，由内核 tick
 * 分频出 1Hz 秒计数，分辨率 1 秒，不占用额外定时器或中断。
 *
 * 若后续启用 STM32 备份域 RTC（LSE）或外接时钟芯片，
 * 只需替换 s_rtc_ops 的 get/set 与 source，上层接口无需改动。
 */

#include "board.h"
#include "ros.h"

static uint32_t s_base_unix = ROS_RTC_DEFAULT_UNIX;
static uint32_t s_base_tick;

static void board_rtc_base_set(uint32_t timestamp, uint32_t tick)
{
    ros_port_critical_enter();
    s_base_unix = timestamp;
    s_base_tick = tick;
    ros_port_critical_exit();
}

static int board_rtc_get(uint32_t *timestamp)
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

static int board_rtc_set(uint32_t timestamp)
{
    board_rtc_base_set(timestamp, ros_tick_get());
    return ROS_OK;
}

static const ros_rtc_ops_t s_rtc_ops = {
    .name = "soft-tick",
    .source = ROS_RTC_SOURCE_TICK,
    .get = board_rtc_get,
    .set = board_rtc_set
};

void board_rtc_init(void)
{
    board_rtc_base_set((uint32_t)ROS_RTC_DEFAULT_UNIX, ros_tick_get());
    (void)ros_rtc_register(&s_rtc_ops);
}
