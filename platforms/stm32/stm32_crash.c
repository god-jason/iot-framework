/*
 * stm32_crash.c - Cortex-M3/M4/M7 硬件故障捕获
 *
 * 触发路径：
 *   HardFault/MemManage/BusFault/UsageFault 中断 -> 裸函数取 MSP/PSP 与
 *   EXC_RETURN -> iot_crash_fault_entry() 采集 CFSR/HFSR/MMFAR/BFAR/AFSR
 *   与异常栈帧 -> 写入 .noinit 记录 -> 打印 -> 按 IOT_CRASH_REBOOT 复位。
 *
 * 关键点：
 *   - 进入异常时 LR 保存 EXC_RETURN，bit2 = 1 表示被中断使用的是 PSP（任务），
 *     为 0 表示 MSP（内核/中断上下文）；
 *   - 只读取 SCB 寄存器与异常栈帧，不做任何动态内存分配；
 *   - 若用户工程自己实现了这些 Handler，请置 IOT_CRASH_OWN_HANDLERS=0，
 *     并在自己的 Handler 里调用 iot_crash_fault_entry()（见 stm32_crash.h）。
 *
 * 该文件不依赖 STM32Cube HAL，只使用 Cortex-M 内核寄存器地址。
 */

#include "iot_crash.h"

#if defined(IOT_PLATFORM_STM32) && IOT_CRASH_ENABLE

#include "stm32_crash.h"

#include <string.h>

/* ------------------------- Cortex-M 内核寄存器 ------------------------- */

#define IOT_SCB_CFSR    (*(volatile uint32_t *)0xE000ED28UL)
#define IOT_SCB_HFSR    (*(volatile uint32_t *)0xE000ED2CUL)
#define IOT_SCB_MMFAR   (*(volatile uint32_t *)0xE000ED34UL)
#define IOT_SCB_BFAR    (*(volatile uint32_t *)0xE000ED38UL)
#define IOT_SCB_AFSR    (*(volatile uint32_t *)0xE000ED3CUL)
#define IOT_SCB_SHCSR   (*(volatile uint32_t *)0xE000ED24UL)
#define IOT_SCB_AIRCR   (*(volatile uint32_t *)0xE000ED0CUL)

#define IOT_SHCSR_MEMFAULTENA   (1UL << 16)
#define IOT_SHCSR_BUSFAULTENA   (1UL << 17)
#define IOT_SHCSR_USGFAULTENA   (1UL << 18)

#define IOT_AIRCR_VECTKEY       (0x5FAUL << 16)
#define IOT_AIRCR_SYSRESETREQ   (1UL << 2)

/* 取异常压栈帧：bit2=1 使用 PSP，否则使用 MSP */
uint32_t *iot_crash_frame(void)
{
    uint32_t *frame;

    __asm volatile(
        "tst  lr, #4        \n"
        "ite  eq            \n"
        "mrseq %0, msp      \n"
        "mrsne %0, psp      \n"
        : "=r"(frame)
        :
        : "memory"
    );
    return frame;
}

/*
 * 异常栈帧是否可读：必须 4 字节对齐且落在 SRAM 常见地址窗内
 * （STM32 F1/F4/H7 的 SRAM/CCM/DTCM 均在 0x10000000~0x30000000）。
 * 栈已严重损坏时放弃读取，避免在故障处理里触发二次异常。
 */
static int iot_frame_readable(const uint32_t *frame)
{
    uintptr_t p = (uintptr_t)frame;

    return (p >= 0x10000000UL && p < 0x30000000UL && (p & 3UL) == 0UL);
}

/* 复位（SYSRESETREQ），记录已写入 .noinit，复位后可读取 */
static void iot_crash_reset(void) __attribute__((noreturn));

static void iot_crash_reset(void)
{
    __asm volatile("cpsid i" ::: "memory");
    IOT_SCB_AIRCR = IOT_AIRCR_VECTKEY | IOT_AIRCR_SYSRESETREQ;
    __asm volatile("dsb" ::: "memory");
    __asm volatile("isb" ::: "memory");
    for (;;) {
    }
}

void iot_crash_fault_entry(uint32_t *frame, uint32_t reason,
                           uint32_t exc_return)
{
    iot_crash_context_t ctx;

    memset(&ctx, 0, sizeof(ctx));

    ctx.exc_return = exc_return;
    if (frame != NULL && iot_frame_readable(frame)) {
        ctx.sp = (uint32_t)(uintptr_t)frame;
        ctx.regs[0] = frame[0];   /* R0 */
        ctx.regs[1] = frame[1];   /* R1 */
        ctx.regs[2] = frame[2];   /* R2 */
        ctx.regs[3] = frame[3];   /* R3 */
        ctx.regs[4] = frame[4];   /* R12 */
        ctx.regs[5] = frame[5];   /* LR */
        ctx.regs[6] = frame[6];   /* PC */
        ctx.regs[7] = frame[7];   /* xPSR */
    }

    ctx.cfsr = IOT_SCB_CFSR;
    ctx.hfsr = IOT_SCB_HFSR;
    ctx.mmfar = IOT_SCB_MMFAR;
    ctx.bfar = IOT_SCB_BFAR;
    ctx.afsr = IOT_SCB_AFSR;
    ctx.code = ctx.cfsr;

    (void)iot_crash_record((int)reason, &ctx);
    iot_crash_print(iot_crash_get());

#if IOT_CRASH_REBOOT
    iot_crash_reset();
#else
    for (;;) {
        /* 停机保留现场，便于调试器连接后查看 */
    }
#endif
}

void iot_crash_port_install(void)
{
    /*
     * 打开独立的 MemManage/BusFault/UsageFault 异常，
     * 否则它们默认升级为 HardFault，丢失具体故障类型。
     */
    IOT_SCB_SHCSR |= IOT_SHCSR_MEMFAULTENA |
                     IOT_SHCSR_BUSFAULTENA |
                     IOT_SHCSR_USGFAULTENA;
    /* 写 1 清除历史故障位，避免上次残留影响本次判断 */
    IOT_SCB_CFSR = IOT_SCB_CFSR;
    IOT_SCB_HFSR = IOT_SCB_HFSR;
}

#if IOT_CRASH_OWN_HANDLERS

/*
 * 裸函数只负责把栈帧、原因、EXC_RETURN 放进 r0/r1/r2，
 * 随后尾跳转到统一的 C 入口，避免在异常现场再构造调用栈。
 */
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst   lr, #4                    \n"
        "ite   eq                        \n"
        "mrseq r0, msp                   \n"
        "mrsne r0, psp                   \n"
        "mov   r2, lr                    \n"
        "movs  r1, #2                    \n"   /* IOT_CRASH_HARD_FAULT */
        "b     iot_crash_fault_entry     \n"
    );
}

__attribute__((naked)) void MemManage_Handler(void)
{
    __asm volatile(
        "tst   lr, #4                    \n"
        "ite   eq                        \n"
        "mrseq r0, msp                   \n"
        "mrsne r0, psp                   \n"
        "mov   r2, lr                    \n"
        "movs  r1, #3                    \n"   /* IOT_CRASH_MEMMANAGE */
        "b     iot_crash_fault_entry     \n"
    );
}

__attribute__((naked)) void BusFault_Handler(void)
{
    __asm volatile(
        "tst   lr, #4                    \n"
        "ite   eq                        \n"
        "mrseq r0, msp                   \n"
        "mrsne r0, psp                   \n"
        "mov   r2, lr                    \n"
        "movs  r1, #4                    \n"   /* IOT_CRASH_BUSFAULT */
        "b     iot_crash_fault_entry     \n"
    );
}

__attribute__((naked)) void UsageFault_Handler(void)
{
    __asm volatile(
        "tst   lr, #4                    \n"
        "ite   eq                        \n"
        "mrseq r0, msp                   \n"
        "mrsne r0, psp                   \n"
        "mov   r2, lr                    \n"
        "movs  r1, #5                    \n"   /* IOT_CRASH_USAGEFAULT */
        "b     iot_crash_fault_entry     \n"
    );
}

#endif /* IOT_CRASH_OWN_HANDLERS */

#endif /* IOT_PLATFORM_STM32 && IOT_CRASH_ENABLE */
