/*
 * board.c - STM32F103CBT6 最小板级支持实现
 *
 * 使用 Keil STM32F1xx_DFP 提供的器件头 stm32f10x.h（寄存器结构体）
 * 与 CMSIS 5.8.0 的 core_cm3.h（SysTick_Config），不依赖 STM32Cube HAL。
 *
 * 时钟：HSE 8MHz -> PLL x9 -> SYSCLK 72MHz，AHB /1，APB1 /2，APB2 /1。
 * tick：SysTick 1ms 中断，其中断服务函数由 libros-stm32-f1.a 的
 *       SysTick_Handler 弱定义提供，内部调用 ros_tick()。
 *
 * 应用级设备注册（PWM / 风扇 / 步进）见 apps/<name>/app_main.c。
 */

#include "board.h"
#include "ros_port.h"

#include "stm32f10x.h"

uint32_t SystemCoreClock = 72000000U;

/*
 * RCC / FLASH / GPIO 位值。器件头 stm32f10x.h 已定义同名宏，
 * 这里统一加 BOARD_ 前缀使用固定数值，避免宏重定义告警，
 * 也不依赖标准外设库的版本差异。
 */
#define BOARD_RCC_CR_HSEON        (1UL << 16)
#define BOARD_RCC_CR_HSERDY       (1UL << 17)
#define BOARD_RCC_CR_PLLON        (1UL << 24)
#define BOARD_RCC_CR_PLLRDY       (1UL << 25)
#define BOARD_RCC_CFGR_PPRE1_DIV2 (1UL << 10)
#define BOARD_RCC_CFGR_PLLSRC_HSE (1UL << 16)
#define BOARD_RCC_CFGR_PLLMULL9   (7UL << 18)
#define BOARD_RCC_CFGR_SW_MASK    0x00000003UL
#define BOARD_RCC_CFGR_SW_PLL     0x00000002UL
#define BOARD_RCC_CFGR_SWS_MASK   0x0000000CUL
#define BOARD_RCC_CFGR_SWS_PLL    0x00000008UL

#define BOARD_FLASH_ACR_PRFTBE    (1UL << 4)
#define BOARD_FLASH_ACR_LATENCY_2 0x00000002UL

static void clock_init(void)
{
    /* 复位后 RCC->CFGR 默认为 0（HSI、无分频），直接置所需位即可 */
    RCC->CR |= BOARD_RCC_CR_HSEON;
    while ((RCC->CR & BOARD_RCC_CR_HSERDY) == 0U) {
        /* 等待外部晶振稳定 */
    }

    /* APB1 最高 36MHz，故 /2；APB2 保持 /1 */
    RCC->CFGR |= BOARD_RCC_CFGR_PPRE1_DIV2;

    /* PLL 输入 = HSE，倍频 x9 = 72MHz */
    RCC->CFGR |= BOARD_RCC_CFGR_PLLSRC_HSE | BOARD_RCC_CFGR_PLLMULL9;

    /* 72MHz 需要 2 个 FLASH 等待周期 */
    FLASH->ACR = BOARD_FLASH_ACR_PRFTBE | BOARD_FLASH_ACR_LATENCY_2;

    RCC->CR |= BOARD_RCC_CR_PLLON;
    while ((RCC->CR & BOARD_RCC_CR_PLLRDY) == 0U) {
        /* 等待 PLL 锁定 */
    }

    RCC->CFGR = (RCC->CFGR & ~BOARD_RCC_CFGR_SW_MASK) | BOARD_RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & BOARD_RCC_CFGR_SWS_MASK) != BOARD_RCC_CFGR_SWS_PLL) {
        /* 等待切换完成 */
    }
}

void SystemInit(void)
{
    clock_init();

    /* 向量表位于 FLASH 起始（VTOR 复位默认 0），无需重映射 */
    (void)SysTick_Config(SystemCoreClock / 1000U);
}

/* ==========================================================================
 * HAL 毫秒时基（AT/ML307 等驱动使用；ROS_TICK_HZ=1000，tick 即毫秒）
 * ========================================================================== */

uint32_t hal_tick_get(void)
{
    /* 用 DWT 微秒时基换算：不依赖内核 tickless 的空闲推进 */
    return ros_port_trace_time_us() / 1000U;
}

void hal_delay_ms(uint32_t ms)
{
    uint32_t start = hal_tick_get();

    while ((uint32_t)(hal_tick_get() - start) < ms) {
        /* 忙等，sys 由 SysTick 中断推进 */
    }
}

/* ==========================================================================
 * 板级早期初始化（由 platforms/stm32/stm32_main.c 的 main 调用）
 * ========================================================================== */

void board_init(void)
{
    /* 时钟与 1ms SysTick 已由启动汇编调用的 SystemInit() 完成，无需额外初始化 */
}
