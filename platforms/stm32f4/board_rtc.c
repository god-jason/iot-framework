/*
 * board_rtc.c - STM32F407VE 板级 RTC 后端
 *
 * 本板未使用硬件 RTC，直接使用平台层共用的 1Hz 软件计时后端
 * （platforms/stm32/stm32_rtc_soft.c）：以 ROS_RTC_DEFAULT_UNIX 为基准，
 * 由内核 tick 分频出 1Hz 秒计数，分辨率 1 秒，不占用额外定时器或中断。
 *
 * 若后续启用 STM32 备份域 RTC（LSE）或外接时钟芯片，只需在此注册硬件
 * 后端，上层接口无需改动。
 */

#include "board.h"
#include "stm32.h"

void board_rtc_init(void)
{
    stm32_rtc_soft_init();
}
