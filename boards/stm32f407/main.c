/*
 * main.c - STM32F407VET6 可烧录固件入口
 *
 * 上电流程：
 *   Reset_Handler -> SystemInit(Cube，向量表/FPU) -> main()
 *   -> HAL_Init() -> SystemClock_Config(168MHz)
 *   -> iot_init() -> 创建 blink 任务 -> iot_sched_start()（不返回）
 *
 * 现象：板载 LED（PA6/PA7）以 1Hz 闪烁；串口 USART1 115200 8N1 可看到
 *       启动日志与 Shell 提示符。
 */

#include "board.h"
#include "iot.h"
#include "shell.h"

#include "stm32_cube.h"

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

    HAL_Init();                 /* SysTick 1ms、NVIC 分组、FLASH 预取 */
    SystemClock_Config();       /* HSE 8MHz -> 168MHz，SysTick 同步重配 */

    iot_init();
    board_led_init();
    board_trace_start(115200U); /* trace 经 USART1/PA9 输出 */

    /* Shell 与 printf 复用同一个 UART1 句柄（PA9 发送 / PA10 接收） */
    (void)shell_platform_start("f407");

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
