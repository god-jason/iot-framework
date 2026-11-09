/*
 * stm32_rtos.c - RTOS 的 Cortex-M 移植层（参考实现）
 *
 * 适用 Cortex-M3/M4/M7（ARMv7-M）。任务使用 PSP，内核/异常使用 MSP。
 *
 * 编译期契约：iot_task.h 中的静态断言保证 iot_task_t.stack_ptr 位于偏移 0，
 * 本文件的汇编直接用 [tcb] 读写栈指针；布局变化会导致编译失败而非运行时错乱。
 *
 * 两条切换路径：
 *   1) 任务上下文主动切换（阻塞/让出/退出）：
 *      iot_port_switch() 保存 r4-r11 与 PSP，屏蔽中断完成寄存器交换；
 *   2) 中断抢占（ISR 唤醒高优先级任务或时间片耗尽）：
 *      iot_port_request_switch() 置位 PendSV，PendSV 全程关中断调用
 *      iot_sched_from_isr() 选出下一个任务后恢复其上下文。
 *
 * 临界区使用 PRIMASK 保存/恢复 + 嵌套计数：
 *   保存最外层进入时的 PRIMASK，退出到最外层时原样恢复，
 *   因此在中断上下文中嵌套调用也不会误开中断。
 *
 * 注意：本文件通过 IOT_PLATFORM_STM32 门控，仅在交叉编译 STM32 时编译。
 */

#if defined(IOT_PLATFORM_STM32)

#include "iot.h"
#include "iot_port.h"

#include <stdint.h>

/* ------------------------- Cortex-M 内核寄存器 ------------------------- */

#define IOT_SCB_ICSR            (*(volatile uint32_t *)0xE000ED04UL)
#define IOT_SCB_ICSR_PENDSVSET  (1UL << 28)
#define IOT_SCB_SHPR3           (*(volatile uint32_t *)0xE000ED20UL)
#define IOT_PENDSV_PRIO_LOWEST  (0xFFUL << 16)   /* PendSV 设为最低优先级 */

/* DWT 周期计数器：trace 的 µs 时间源（Cortex-M3/M4/M7 均有） */
#define IOT_DEMCR               (*(volatile uint32_t *)0xE000EDFCUL)
#define IOT_DEMCR_TRCENA        (1UL << 24)
#define IOT_DWT_CTRL            (*(volatile uint32_t *)0xE0001000UL)
#define IOT_DWT_CYCCNT          (*(volatile uint32_t *)0xE0001004UL)
#define IOT_DWT_CTRL_CYCCNTENA  (1UL << 0)

/* SysTick：tickless 用同一个定时器做长睡眠唤醒，无需额外低功耗外设 */
#define IOT_SYSTICK_CTRL        (*(volatile uint32_t *)0xE000E010UL)
#define IOT_SYSTICK_LOAD        (*(volatile uint32_t *)0xE000E014UL)
#define IOT_SYSTICK_VAL         (*(volatile uint32_t *)0xE000E018UL)

#ifndef IOT_TRACE_CLOCK_HZ
#define IOT_TRACE_CLOCK_HZ      72000000UL      /* SYSCLK，按板级时钟覆盖 */
#endif
#ifndef IOT_SYSTICK_CLOCK_HZ
#define IOT_SYSTICK_CLOCK_HZ    IOT_TRACE_CLOCK_HZ
#endif

static volatile uint32_t s_tickless_active;
static volatile uint32_t s_tickless_expired;

static volatile uint32_t s_critical_nesting;
static uint32_t s_critical_primask;

/*
 * µs 单调时间戳（覆盖 iot_trace.c 的 weak 默认实现）。
 * 首次调用惰性启用 DWT->CYCCNT；CYCCNT 约 59.6s 回绕，
 * 换算成 µs 后是 32 位 µs（约 71 分钟），主机侧按单调递增处理。
 */
uint32_t iot_port_trace_time_us(void)
{
    uint32_t hz_per_us = IOT_TRACE_CLOCK_HZ / 1000000UL;

    if ((IOT_DWT_CTRL & IOT_DWT_CTRL_CYCCNTENA) == 0U) {
        IOT_DEMCR |= IOT_DEMCR_TRCENA;
        IOT_DWT_CYCCNT = 0U;
        IOT_DWT_CTRL |= IOT_DWT_CTRL_CYCCNTENA;
    }
    if (hz_per_us == 0U) {
        hz_per_us = 1U;
    }
    return IOT_DWT_CYCCNT / hz_per_us;
}

/* ------------------------- 临界区（PRIMASK） ------------------------- */

static inline uint32_t iot_irq_save(void)
{
    uint32_t primask;

    __asm volatile ("mrs %0, primask" : "=r" (primask) :: "memory");
    __asm volatile ("cpsid i" ::: "memory");
    return primask;
}

static inline void iot_irq_restore(uint32_t primask)
{
    /* 只有进入前中断是打开的，才在退出时重新打开 */
    if ((primask & 1U) == 0U) {
        __asm volatile ("cpsie i" ::: "memory");
    }
}

void iot_port_init(void)
{
    s_critical_nesting = 0U;
    s_critical_primask = 0U;

    /* PendSV 设为最低优先级，保证不会打断其它 ISR */
    IOT_SCB_SHPR3 |= IOT_PENDSV_PRIO_LOWEST;
}

void iot_port_critical_enter(void)
{
    uint32_t primask = iot_irq_save();

    if (s_critical_nesting == 0U) {
        s_critical_primask = primask;   /* 只记录最外层状态 */
    }
    s_critical_nesting++;
}

void iot_port_critical_exit(void)
{
    if (s_critical_nesting > 0U) {
        s_critical_nesting--;
    }

    if (s_critical_nesting == 0U) {
        iot_irq_restore(s_critical_primask);
    }
}

bool iot_port_in_isr(void)
{
    uint32_t ipsr;

    __asm volatile ("mrs %0, ipsr" : "=r" (ipsr) :: "memory");
    return (ipsr & 0x1FFUL) != 0U;   /* 异常号 0 = 线程模式 */
}

/* ------------------------- 任务栈与上下文 ------------------------- */

/* 任务入口返回后跳到这里退出任务 */
__attribute__((naked)) static void iot_task_trampoline(void)
{
    __asm volatile(
        "bl  iot_task_exit   \n"
        "b   .                \n"
    );
}

int iot_port_stack_init(iot_task_t *task)
{
    uint8_t *top;
    uint32_t *sp;
    int i;

    if (task == NULL || task->stack_mem == NULL ||
        task->stack_size < (16U * sizeof(uint32_t))) {
        return IOT_INVALID_PARAM;
    }

    top = (uint8_t *)task->stack_mem + task->stack_size;
    sp = (uint32_t *)((uintptr_t)top & ~7UL);   /* 8 字节对齐 */

    /* 硬件异常返回帧（高地址 → 低地址） */
    *--sp = 0x01000000UL;                   /* xPSR（Thumb 位） */
    *--sp = (uint32_t)task->entry;          /* PC */
    *--sp = (uint32_t)iot_task_trampoline;  /* LR */
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
    return IOT_OK;
}

void iot_port_stack_deinit(iot_task_t *task)
{
    /* Cortex-M 的上下文保存在任务栈内，随任务栈一起释放，无独立资源 */
    (void)task;
}

/*
 * 首个任务的栈指针，由 SVC_Handler 读取。必须是全局符号：
 * naked 汇编用 movw/movt 取地址，不依赖 literal pool。
 */
void *iot_first_task_sp = NULL;

/*
 * 启动第一个任务：触发 SVC，在异常上下文中切换到 PSP 并异常返回。
 * 不能用线程模式下的 `bx 0xFFFFFFFD` —— EXC_RETURN 只在 Handler 模式有效，
 * 在线程模式执行会直接触发 HardFault。
 */
void iot_port_start_first(iot_task_t *task)
{
    iot_first_task_sp = task->stack_ptr;
    __asm volatile ("svc #0" ::: "memory");
    for (;;) {
        /* SVC 异常返回后已进入任务上下文，不会回到这里 */
    }
}

/*
 * SVC_Handler：恢复首个任务的 r4-r11，令 PSP 指向其硬件异常帧，
 * 再以 EXC_RETURN=0xFFFFFFFD 返回线程模式（使用 PSP），
 * 由硬件自动弹出 r0-r3/r12/lr/PC/xPSR 跳入任务入口。
 */
__attribute__((weak, naked)) void SVC_Handler(void)
{
    __asm volatile(
        "movw  r0, #:lower16:iot_first_task_sp \n"
        "movt  r0, #:upper16:iot_first_task_sp \n"
        "ldr   r0, [r0]                       \n"
        "ldmia r0!, {r4-r11}                  \n"
        "msr   psp, r0                        \n"
        "mov   r1, #2                         \n"
        "msr   control, r1                   \n"   /* 线程模式改用 PSP */
        "isb                                  \n"
        "movw  r1, #0xFFFD                    \n"
        "movt  r1, #0xFFFF                    \n"
        "mov   lr, r1                         \n"   /* EXC_RETURN */
        "bx    lr                             \n"
    );
}

/*
 * 任务上下文主动切换。
 * 进入时关中断，避免 ISR 在 PSP 切换过程中压栈；切到目标任务后开中断。
 * 所有调用点都在临界区之外，因此 PRIMASK 在每个任务内是平衡的。
 */
__attribute__((naked)) void iot_port_switch(iot_task_t *from, iot_task_t *to)
{
    (void)from;
    (void)to;
    __asm volatile(
        "cpsid i              \n"
        "mrs   r2, psp         \n"   /* r2 = 当前 PSP */
        "stmdb r2!, {r4-r11}   \n"   /* 保存 r4-r11，r2 递减 */
        "str   r2, [r0]        \n"   /* from->stack_ptr = r2 */
        "ldr   r2, [r1]        \n"   /* r2 = to->stack_ptr */
        "ldmia r2!, {r4-r11}   \n"   /* 恢复 r4-r11 */
        "msr   psp, r2         \n"
        "cpsie i              \n"   /* 新任务从这里恢复并开中断 */
        "bx    lr              \n"
    );
}

void iot_port_return(void)
{
    /* 单片机端 iot_stop 不返回，这里只占位 */
    for (;;) {
    }
}

/* ------------------------- 抢占（PendSV） ------------------------- */

void iot_port_request_switch(void)
{
    IOT_SCB_ICSR = IOT_SCB_ICSR_PENDSVSET;
}

/*
 * PendSV：全程屏蔽中断，保存被打断任务的 r4-r11 与 PSP，
 * 调用 iot_sched_from_isr() 选择下一个任务，再恢复其上下文。
 * 因此 iot_sched_from_isr() 不再自行开关临界区（避免异常中途 cpsie i
 * 导致 PendSV 被嵌套、PSP 半成品状态被别的 ISR 看到）。
 * 硬件在异常返回时自动弹出 r0-r3/r12/lr/pc/xPSR。
 */
__attribute__((weak, naked)) void PendSV_Handler(void)
{
    __asm volatile(
        "cpsid i                            \n"
        "mrs   r1, psp                      \n"
        "stmdb r1!, {r4-r11}                \n"   /* 保存到被打断任务的栈 */

        "movw  r2, #:lower16:iot_current_task \n"
        "movt  r2, #:upper16:iot_current_task \n"
        "ldr   r2, [r2]                     \n"   /* r2 = 被打断的任务 */
        "str   r1, [r2]                     \n"   /* task->stack_ptr = r1 */

        "push  {r2, lr}                     \n"   /* 保护 lr 与当前任务指针 */
        "bl    iot_sched_from_isr           \n"   /* 返回下一个任务或 NULL */
        "pop   {r2, lr}                     \n"

        "cmp   r0, #0                       \n"
        "bne   1f                           \n"
        "mov   r0, r2                       \n"   /* 不切换：恢复当前任务 */
        "1:                                 \n"
        "ldr   r0, [r0]                     \n"   /* r0 = task->stack_ptr */
        "ldmia r0!, {r4-r11}                \n"
        "msr   psp, r0                      \n"
        "cpsie i                            \n"
        "bx    lr                           \n"
    );
}

/*
 * SysTick 默认处理：推进 tick。tick 内部会在时间片耗尽或
 * iot_task_wake() 唤醒高优先级任务时调用 iot_port_request_switch()。
 * 工程若已有 SysTick_Handler，本弱符号被覆盖（需自行调用 iot_tick()）。
 */
__attribute__((weak)) void SysTick_Handler(void)
{
    if (s_tickless_active) {
        /* 长睡眠到期：只置标志，由 iot_port_idle_timeout() 统一补偿 tick */
        s_tickless_expired = 1U;
        return;
    }
    iot_tick();
}

/* ------------------------- 空闲 ------------------------- */

void iot_port_idle(void)
{
    __asm volatile ("wfi");
}

/*
 * tickless 空闲：把 SysTick 从 1ms 周期临时改成 ms 毫秒的一次性唤醒，
 * 进入 WFI；唤醒后按实际经过时间一次性补偿内核 tick。
 *
 * 两种唤醒来源：
 *   - SysTick 到期：SysTick_Handler 只置 s_tickless_expired，补偿 sleep_ms；
 *   - 其它外设中断提前唤醒：读 SysTick->VAL 算出已过去的 tick，按实际值补偿。
 * 补偿统一走 iot_tick_increment(elapsed)，到期任务、软件定时器、
 * 时间片轮转与抢占请求都会在一次调用里被正确处理。
 *
 * 约束：SysTick_Handler 需使用本文件的弱实现（或在自己的实现里转发标志），
 * 且 IOT_SYSTICK_CLOCK_HZ 必须与板级 SysTick 时钟一致。
 */
void iot_port_idle_timeout(uint32_t ms)
{
    uint32_t ticks_per_ms = IOT_SYSTICK_CLOCK_HZ / 1000UL;
    uint32_t sleep_ms;
    uint32_t max_ms;
    uint32_t load;
    uint32_t val;
    uint32_t elapsed;

    if (ticks_per_ms == 0U) {
        ticks_per_ms = 1U;
    }

    sleep_ms = ((ms == 0U) || (ms == UINT32_MAX)) ? 1U : ms;
    max_ms = 0x00FFFFFFUL / ticks_per_ms;   /* SysTick LOAD 为 24 位 */
    if (sleep_ms > max_ms) {
        sleep_ms = max_ms;
    }
    load = sleep_ms * ticks_per_ms - 1U;

    /* 关中断重配 SysTick，避免配置过程中断打断 */
    __asm volatile ("cpsid i" ::: "memory");
    IOT_SYSTICK_LOAD = load;
    IOT_SYSTICK_VAL = 0U;
    s_tickless_expired = 0U;
    s_tickless_active = 1U;
    __asm volatile ("cpsie i" ::: "memory");

    IOT_TRACE(IOT_EVT_TICKLESS_ENTER, sleep_ms, load);
    __asm volatile ("wfi" ::: "memory");

    /* 唤醒后立即恢复 1ms 周期；顺便清掉 SysTick pending，
     * 避免开中断后又执行一次 SysTick_Handler 多计 1 tick */
    __asm volatile ("cpsid i" ::: "memory");
    s_tickless_active = 0U;
    val = IOT_SYSTICK_VAL;
    IOT_SYSTICK_LOAD = ticks_per_ms - 1U;
    IOT_SYSTICK_VAL = 0U;
    IOT_SCB_ICSR = (1UL << 26);      /* PENDSTCLR */
    __asm volatile ("cpsie i" ::: "memory");

    if (s_tickless_expired) {
        elapsed = sleep_ms;
    } else if (val <= load) {
        elapsed = (load - val) / ticks_per_ms;   /* 被其它中断提前唤醒 */
    } else {
        elapsed = 0U;
    }
    s_tickless_expired = 0U;

    IOT_TRACE(IOT_EVT_TICKLESS_EXIT, elapsed, val);

    if (elapsed > 0U) {
        iot_tick_increment(elapsed);
    }
}

#endif /* IOT_PLATFORM_STM32 */
