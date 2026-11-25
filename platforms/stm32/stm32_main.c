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
#include "boot_meta.h"

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
 * HardFault：crash 计数写入 KV，现场（PC/LR/寄存器/故障状态/栈快照）
 * 直接打印到 USART1，随后复位。
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

/*
 * 崩溃现场直接输出到 USART1（轮询，不依赖 HAL / RTOS / Flash）。
 *
 * HardFault 上下文只剩寄存器，这里用 board_trace_start 已配置好的
 * USART1（115200 8N1）逐字符输出，格式与 tools/crash_log.py 的解析规则
 * 一致（reason=/pc=/lr=/sp=/xpsr=/cfsr=/hfsr=/excr=/stack[]）。
 */
#define CRASH_USART_SR_TXE      (1UL << 7)

static void crash_putc(char c)
{
    while ((USART1->SR & CRASH_USART_SR_TXE) == 0U) {
    }
    USART1->DR = (uint32_t)(uint8_t)c;
}

static void crash_puts(const char *s)
{
    while (*s != '\0') {
        crash_putc(*s++);
    }
}

static void crash_put_hex(uint32_t v)
{
    int shift;

    for (shift = 28; shift >= 0; shift -= 4) {
        uint32_t d = (v >> (uint32_t)shift) & 0x0FU;

        crash_putc((char)(d < 10U ? ('0' + d) : ('A' + d - 10U)));
    }
}

static void crash_put_dec(uint32_t v)
{
    char buf[10];
    uint32_t n = 0U;

    if (v == 0U) {
        crash_putc('0');
        return;
    }
    while (v > 0U && n < sizeof(buf)) {
        buf[n++] = (char)('0' + (v % 10U));
        v /= 10U;
    }
    while (n > 0U) {
        crash_putc(buf[--n]);
    }
}

void stm32_hardfault_report(uint32_t *frame, uint32_t exc_return, uint32_t sp)
{
    boot_state_t s;
    uint32_t addr;
    uint32_t words;
    uint32_t i;

    /* crash 累计并写回 KV（Bootloader 据此判断启动失败） */
    boot_state_load(&s);
    if (s.crash < 0xFFFFFFFFU) {
        s.crash++;
    }
    (void)boot_state_save(&s);

    crash_puts("\r\n[CRASH] reason=HARDFAULT" FLASH_CRASH_TAG " crash=");
    crash_put_dec(s.crash);
    crash_puts("\r\n");

    if (frame == NULL) {
        crash_puts("[CRASH] no exception frame, reset\r\n");
        NVIC_SystemReset();
        for (;;) {
        }
    }

    crash_puts("pc=");
    crash_put_hex(frame[6]);
    crash_puts(" lr=");
    crash_put_hex(frame[5]);
    crash_puts(" sp=");
    crash_put_hex(sp);
    crash_puts(" xpsr=");
    crash_put_hex(frame[7]);
    crash_puts("\r\n");

    crash_puts("r0=");
    crash_put_hex(frame[0]);
    crash_puts(" r1=");
    crash_put_hex(frame[1]);
    crash_puts(" r2=");
    crash_put_hex(frame[2]);
    crash_puts(" r3=");
    crash_put_hex(frame[3]);
    crash_puts(" r12=");
    crash_put_hex(frame[4]);
    crash_puts("\r\n");

    crash_puts("cfsr=");
    crash_put_hex(SCB->CFSR);
    crash_puts(" hfsr=");
    crash_put_hex(SCB->HFSR);
    crash_puts(" excr=");
    crash_put_hex(exc_return);
    crash_puts("\r\n");

    crash_puts("mmfar=");
    crash_put_hex(SCB->MMFAR);
    crash_puts(" bfar=");
    crash_put_hex(SCB->BFAR);
    crash_puts("\r\n");

    /* 栈快照：从异常栈指针起最多 128 字，每行 8 字 */
    addr = sp & ~3U;
    if (addr > FLASH_SRAM_END) {
        addr = FLASH_SRAM_END;
    }
    words = 128U;
    if (((FLASH_SRAM_END - addr) / 4U) < words) {
        words = (FLASH_SRAM_END - addr) / 4U;
    }
    i = 0U;
    while (i < words) {
        uint32_t j;

        crash_puts("stack[");
        crash_put_hex(addr + i * 4U);
        crash_puts("]:");
        for (j = 0U; j < 8U && i < words; j++, i++) {
            crash_putc(' ');
            crash_put_hex(*(const volatile uint32_t *)(uintptr_t)
                          (addr + i * 4U));
        }
        crash_puts("\r\n");
    }

    crash_puts("[CRASH] reset\r\n");
    NVIC_SystemReset();

    for (;;) {
    }
}
