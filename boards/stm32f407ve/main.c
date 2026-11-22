/*
 * main.c - STM32F407VET6 可烧录固件入口
 *
 * 上电流程：
 *   Reset_Handler -> SystemInit(Cube，向量表/FPU) -> main()
 *   -> HAL_Init() -> SystemClock_Config(168MHz)
 *   -> ros_init() -> 板级外设/命令 -> 创建 blink 任务 -> ros_sched_start()
 *
 * 与 stm32f103cb 一致：APP/BACKUP/OTA（APP 主程序 / BACKUP 备份 / OTA 升级包）、
 * PWM/风扇/步进/RTC/date 命令、HardFault 崩溃记录与 boot_confirm()。
 */

#include "board.h"
#include "ros.h"
#include "common.h"
#include "roshell.h"
#include "device.h"
#include "datetime.h"

#include "stm32_cube.h"
#include "boot_meta.h"
#include "ros_rtc.h"

/* 本固件运行在 APP 区，向量表基址固定为 0x08020000（BACKUP/OTA 不直接运行） */
#ifndef APP_VECTOR_BASE
#define APP_VECTOR_BASE 0x08020000U
#endif

static ros_task_t s_blink_task;
static uint8_t    s_blink_stack[1024];

static void blink_entry(void *arg)
{
    (void)arg;

    for (;;) {
        board_led_toggle();
        ros_sleep_ms(500U);
    }
}

int main(void)
{
    int rc;

    HAL_Init();                 /* SysTick 1ms、NVIC 分组、FLASH 预取 */
    SystemClock_Config();       /* HSE 8MHz -> 168MHz */

    ros_init();
    board_led_init();
    board_trace_start(115200U); /* trace 经 USART1/PA9 输出 */

    /* APP 区固件：向量表重定位到 APP 区基址 */
    SCB->VTOR = APP_VECTOR_BASE;

    /* roshell 与 printf 复用同一个 UART1 句柄（PA9 发送 / PA10 接收） */
    (void)rs_platform_start("f407ve");

    /* 板级功能：PWM/风扇/步进/RTC/date 命令 */
    if (device_init() == 0) {
        board_pwm_register();
        board_fan_init();
    }
    board_stepper_init();
    board_rtc_init();
    (void)datetime_cmd_register();

    rc = ros_task_create(&s_blink_task, "blink", 2U, blink_entry, NULL,
                         s_blink_stack, sizeof(s_blink_stack));
    if (rc != ROS_OK) {
        for (;;) {
            board_led_toggle();
            for (volatile uint32_t i = 0U; i < 100000U; i++) {
            }
        }
    }

    /* 初始化完成：向 Bootloader 确认本次启动成功 */
    boot_confirm();

    ros_sched_start();

    for (;;) {
        /* MCU 端不返回 */
    }
}

/*
 * HardFault：提取异常栈帧并写入 Flash 崩溃日志，随后复位。
 * Bootloader 下次启动打印现场；crash 超过上限时用 BACKUP 恢复 APP 区。
 */
void hardfault_report(uint32_t *frame, uint32_t exc_return, uint32_t sp);

void HardFault_Handler(void) __attribute__((naked));
void HardFault_Handler(void)
{
    __asm(
        "tst lr, #4         \n"
        "ite eq             \n"
        "mrseq r0, msp      \n"
        "mrsne r0, psp      \n"
        "mov r1, lr         \n"
        "mov r2, r0         \n"
        "mrs r3, control    \n"
        "bic r3, r3, #2     \n"
        "msr control, r3    \n"
        "isb                \n"
        "b hardfault_report \n"
    );
}

void hardfault_report(uint32_t *frame, uint32_t exc_return, uint32_t sp)
{
    boot_crash_log(frame, sp, exc_return, ros_rtc_get_timestamp());
    NVIC_SystemReset();

    for (;;) {
    }
}
