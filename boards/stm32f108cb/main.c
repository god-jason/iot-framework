/*
 * main.c - STM32F103C8T6 可烧录固件入口
 *
 * 上电流程：
 *   Reset_Handler -> SystemInit(时钟 + 1ms SysTick) -> main()
 *   -> ros_init() -> 创建 blink 任务 -> ros_sched_start()（不返回）
 *
 * 现象：板载 LED（PC13）以 1Hz 闪烁，说明调度器、SysTick、时间片轮换
 *       与 PendSV 抢占链路均正常工作。
 */

#include "board.h"
#include "ros.h"
#include "common.h"
#include "roshell.h"
#include "device.h"
#include "datetime.h"

#include "stm32f10x.h"
#include "boot_meta.h"

/* 本固件所在 A/B 槽的向量表基址；B 槽构建时由 -DAPP_VECTOR_BASE=0x08010000U 覆盖 */
#ifndef APP_VECTOR_BASE
#define APP_VECTOR_BASE 0x08002000U
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

    ros_init();
    board_led_init();
    board_trace_start(115200U);   /* trace 经 USART1/PA9 输出，串口助手 115200 8N1 */

    /* A/B 槽固件：向量表重定位到本槽基址 */
    SCB->VTOR = APP_VECTOR_BASE;

    /* roshell 默认绑定 UART1（PA9 发送 / PA10 接收）；
     * log 的 printf 由 syscalls.c 的 _write 同样走 UART1 */
    (void)rs_platform_start("f108cb");

    /* 注册板级 RTC 后端（本板无硬件时钟，用 1Hz 软件计时） */
    board_rtc_init();

    /* 注册 gpio / pwm 命令，并由板级注册 pwm0（TIM2_CH1 / PA0） */
    if (device_init() == 0) {
        board_pwm_register();
        board_fan_init();          /* 风扇复用 pwm0（TIM2_CH1 / PA0） */
    }

    /* 步进电机（STEP=PA4 / DIR=PA5），注册 stepper 命令 */
    board_stepper_init();

    /* date 命令：读写系统时间（ros_rtc，硬件优先，否则 1Hz 软件计时） */
    (void)datetime_cmd_register();

    rc = ros_task_create(&s_blink_task, "blink", 2U, blink_entry, NULL,
                         s_blink_stack, sizeof(s_blink_stack));
    if (rc != ROS_OK) {
        /* 创建失败：快速闪灯示意（正常不会发生） */
        for (;;) {
            board_led_toggle();
            for (volatile uint32_t i = 0U; i < 100000U; i++) {
            }
        }
    }

    /* 初始化完成：向 Bootloader 确认本次启动成功。
     * 升级后的新槽若连续 3 次未确认，Bootloader 会自动回滚到旧槽。 */
    boot_confirm();

    ros_sched_start();

    for (;;) {
        /* MCU 端不返回 */
    }
}

/*
 * HardFault：提取异常栈帧（PC/LR），把崩溃日志写入 Flash 文本区并累加计数，
 * 随后复位。Bootloader 下次启动发现 crash > BOOT_MAX_CRASH 时回退到另一分区。
 */
void hardfault_report(uint32_t *frame, uint32_t exc_return, uint32_t sp);

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
        "b hardfault_report \n"
    );
}

void hardfault_report(uint32_t *frame, uint32_t exc_return, uint32_t sp)
{
    /* 记录崩溃现场（寄存器/故障状态/栈快照）并累加计数，然后复位 */
    boot_crash_log(frame, sp, exc_return);
    NVIC_SystemReset();

    for (;;) {
    }
}
