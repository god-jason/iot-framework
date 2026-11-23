/*
 * stm32_main.c - STM32 固件入口（F1/F4 共用）
 *
 * 上电流程：
 *   Reset_Handler -> SystemInit（时钟 + 1ms SysTick）-> main()
 *   -> board_init()：板级早期初始化（F4 的 HAL_Init + 时钟树）
 *   -> ros_init() -> 基础外设/命令 -> 创建 app 任务 -> ros_sched_start()
 *
 * 应用级内容（blink、PWM/风扇/步进注册等）由 apps/<name>/app_main.c 负责；
 * 板级差异通过 platforms/stm32<family>/board.h 的 BOARD_* 参数与 board_init() 提供。
 */

#include "board.h"

#include "ros.h"
#include "roshell.h"
#include "datetime.h"
#include "app.h"
#include "ros_rtc.h"
#include "boot_meta.h"
#include "boot_crash.h"

/* 芯片头：提供 SCB / NVIC_SystemReset 等 */
#if defined(ROS_STM32_F1)
#include "stm32f10x.h"
#elif defined(ROS_STM32_F4)
#include "stm32_cube.h"
#endif

/* 应用任务：入口为弱符号 app_main，可链接 apps/<name> 覆盖实现 */
static ros_task_t s_app_task;
static uint8_t    s_app_stack[BOARD_APP_STACK_SIZE];

int main(void)
{
    /* 板级早期初始化（时钟等） */
    board_init();

    ros_init();
    board_trace_start(115200U);   /* trace 经 USART1/PA9 输出，串口助手 115200 8N1 */

    /* App 区固件：向量表重定位到 App 区基址 */
    SCB->VTOR = BOARD_APP_VECTOR_BASE;

    /* roshell 绑定 UART1；printf 由 stm32_syscalls.c 的 _write 同样走 UART1 */
    (void)rs_platform_start(BOARD_NAME);

    /* 注册板级 RTC 后端（硬件优先，否则 1Hz 软件计时） */
    board_rtc_init();

    /* date 命令：读写系统时间 */
    (void)datetime_cmd_register();

    /* 应用级入口：独立任务运行（apps/<name> 提供实现）；
     * 优先级低于板级基础任务，不抢占 LED/串口等基础功能 */
    (void)ros_task_create(&s_app_task, "app", 6U, app_main, NULL,
                          s_app_stack, sizeof(s_app_stack));

    /* 初始化完成：向 Bootloader 确认启动成功，tries/crash 清零 */
    boot_confirm();

    ros_sched_start();

    for (;;) {
        /* MCU 端不返回 */
    }
}

/*
 * HardFault：crash 累计并写回状态，异常栈帧（PC/LR 等）带 Unix 时间追加到
 * 错误日志，随后复位。F4 由 Bootloader 在 crash 超限时用 BACKUP 恢复。
 */
void stm32_hardfault_report(uint32_t *frame, uint32_t exc_return, uint32_t sp);

void HardFault_Handler(void) __attribute__((naked));
void HardFault_Handler(void)
{
    /* r0=异常栈帧指针，r1=EXC_RETURN，r2=异常发生时栈指针；随后切回 MSP */
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
        "b stm32_hardfault_report \n"
    );
}

void stm32_hardfault_report(uint32_t *frame, uint32_t exc_return, uint32_t sp)
{
    boot_crash_log(frame, sp, exc_return, ros_rtc_get_timestamp());
    NVIC_SystemReset();

    for (;;) {
    }
}
