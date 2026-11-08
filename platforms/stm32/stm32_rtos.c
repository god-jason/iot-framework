/*
 * stm32_rtos.c - RTOS 的 Cortex-M 移植层（参考实现，未在当前 Windows 构建编译）
 *
 * 适用于 Cortex-M3/M4/M7（ARMv7-M）。任务使用 PSP，内核使用 MSP。
 * 协作式上下文切换 iot_port_switch 直接保存/恢复 r4-r11；
 * 抢占式（SysTick -> PendSV 延迟切换）需与目标工程的向量表集成，
 * 用户可在 SysTick_Handler 中调用 iot_tick()，并在需要抢占处触发 PendSV。
 *
 * 注意：
 *   - 本文件通过 IOT_PLATFORM_STM32 门控，仅在交叉编译 STM32 目标时编译；
 *   - 首次启动通过 EXC_RETURN(0xFFFFFFFD, 线程模式 + PSP) 恢复任务帧。
 */

#if defined(IOT_PLATFORM_STM32)

#include "iot.h"
#include "port.h"
#include "internal.h"

#include <stdint.h>

static volatile uint32_t s_critical_nesting;

static inline void iot_irq_disable(void)
{
    __asm volatile("cpsid i" ::: "memory");
}

static inline void iot_irq_enable(void)
{
    __asm volatile("cpsie i" ::: "memory");
}

void iot_port_init(void)
{
    s_critical_nesting = 0U;
}

void iot_port_critical_enter(void)
{
    iot_irq_disable();
    s_critical_nesting++;
}

void iot_port_critical_exit(void)
{
    if (s_critical_nesting > 0U) {
        s_critical_nesting--;
    }
    if (s_critical_nesting == 0U) {
        iot_irq_enable();
    }
}

/* 任务入口返回后跳到这里退出任务 */
__attribute__((naked)) static void iot_task_trampoline(void)
{
    __asm volatile(
        "bl  iot_task_exit   \n"
        "b   .                \n"
    );
}

void iot_port_stack_init(iot_task_t *task)
{
    uint8_t *top = (uint8_t *)task->stack_mem + task->stack_size;
    uint32_t *sp = (uint32_t *)((uintptr_t)top & ~7UL);   /* 8 字节对齐 */
    int i;

    /* 硬件异常返回帧（高地址 → 低地址） */
    *--sp = 0x01000000UL;                   /* xPSR（Thumb 位） */
    *--sp = (uint32_t)task->entry;          /* PC */
    *--sp = (uint32_t)iot_task_trampoline; /* LR */
    *--sp = 0UL;                            /* R12 */
    *--sp = 0UL;                            /* R3 */
    *--sp = 0UL;                            /* R2 */
    *--sp = 0UL;                            /* R1 */
    *--sp = (uint32_t)task->arg;            /* R0 */

    /* 软件保存的寄存器 r4-r11 */
    for (i = 0; i < 8; i++) {
        *--sp = 0UL;
    }

    task->stack_ptr = sp;
}

__attribute__((naked)) void iot_port_start_first(iot_task_t *task)
{
    (void)task;
    __asm volatile(
        "ldr   r0, [r0]        \n"   /* r0 = task->stack_ptr */
        "msr   psp, r0         \n"
        "mov   r1, #2          \n"
        "msr   control, r1     \n"   /* CONTROL.SPSEL=1：线程模式使用 PSP */
        "isb                  \n"
        "ldmia r0!, {r4-r11}   \n"   /* 恢复 r4-r11 */
        "msr   psp, r0         \n"   /* PSP 指向硬件返回帧 */
        "movw  r1, #0xFFFD     \n"
        "movt  r1, #0xFFFF     \n"   /* EXC_RETURN = 0xFFFFFFFD */
        "bx    r1              \n"
    );
}

__attribute__((naked)) void iot_port_switch(iot_task_t *from, iot_task_t *to)
{
    (void)from;
    (void)to;
    __asm volatile(
        "mrs   r2, psp         \n"   /* r2 = 当前 PSP */
        "stmdb r2!, {r4-r11}   \n"   /* 保存 r4-r11，r2 递减 */
        "str   r2, [r0]        \n"   /* from->stack_ptr = r2 */
        "ldr   r2, [r1]        \n"   /* r2 = to->stack_ptr */
        "ldmia r2!, {r4-r11}   \n"   /* 恢复 r4-r11 */
        "msr   psp, r2         \n"
        "bx    lr              \n"
    );
}

void iot_port_return(void)
{
    /* 单片机端 iot_stop 不返回，这里只占位 */
    for (;;) {
    }
}

#endif /* IOT_PLATFORM_STM32 */