/*
 * main.c - STM32F103C8T6 可烧录固件入口
 *
 * 上电流程：
 *   Reset_Handler -> SystemInit(时钟 + 1ms SysTick) -> main()
 *   -> iot_init() -> 创建 blink 任务 -> iot_sched_start()（不返回）
 *
 * 现象：板载 LED（PC13）以 1Hz 闪烁，说明调度器、SysTick、时间片轮换
 *       与 PendSV 抢占链路均正常工作。
 */

#include "board.h"
#include "iot.h"

static iot_task_t s_blink_task;
static uint8_t    s_blink_stack[1024];

static void blink_entry(void *arg)
{
    (void)arg;

    for (;;) {
        board_led_toggle();
        iot_sleep_ms(500U);
    }
}

int main(void)
{
    int rc;

    iot_init();
    board_led_init();
    board_trace_start(115200U);   /* trace 经 USART1/PA9 输出，串口助手 115200 8N1 */

    rc = iot_task_create(&s_blink_task, "blink", 2U, blink_entry, NULL,
                         s_blink_stack, sizeof(s_blink_stack));
    if (rc != IOT_OK) {
        /* 创建失败：快速闪灯示意（正常不会发生） */
        for (;;) {
            board_led_toggle();
            for (volatile uint32_t i = 0U; i < 100000U; i++) {
            }
        }
    }

    iot_sched_start();

    for (;;) {
        /* MCU 端不返回 */
    }
}
