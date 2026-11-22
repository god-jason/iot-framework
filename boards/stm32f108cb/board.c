/*
 * board.c - STM32F103C8T6 最小板级支持实现
 *
 * 使用 Keil STM32F1xx_DFP 提供的器件头 stm32f10x.h（寄存器结构体）
 * 与 CMSIS 5.8.0 的 core_cm3.h（SysTick_Config），不依赖 STM32Cube HAL。
 *
 * 时钟：HSE 8MHz -> PLL x9 -> SYSCLK 72MHz，AHB /1，APB1 /2，APB2 /1。
 * tick：SysTick 1ms 中断，其中断服务函数由 libros-stm32-f1.a 的
 *       SysTick_Handler 弱定义提供，内部调用 ros_tick()。
 */

#include "board.h"
#include "device.h"

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
#define BOARD_RCC_APB2ENR_IOPCEN  (1UL << 4)

#define BOARD_FLASH_ACR_PRFTBE    (1UL << 4)
#define BOARD_FLASH_ACR_LATENCY_2 0x00000002UL

#define LED_PIN                   13U

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

void board_led_init(void)
{
    RCC->APB2ENR |= BOARD_RCC_APB2ENR_IOPCEN;

    /* PC13 属于 CRH，位段 [23:20]：MODE=10(2MHz 输出) CNF=00(推挽) */
    GPIOC->CRH &= ~(0x0FUL << ((LED_PIN - 8U) * 4U));
    GPIOC->CRH |=  (0x02UL << ((LED_PIN - 8U) * 4U));

    /* 低电平点亮，初始置高 = 灭 */
    GPIOC->BSRR = (1UL << LED_PIN);
}

void board_led_toggle(void)
{
    GPIOC->ODR ^= (1UL << LED_PIN);
}

/* 向 device 组件注册本板 PWM 通道（只绑定句柄，不做硬件初始化）
 *   pwm0~pwm3  = TIM2_CH1~4（PA0~PA3），默认 1kHz
 *   pwm4~pwm7  = TIM3_CH1~4（PA6/PA7/PB0/PB1），默认 2kHz
 *   pwm8~pwm11 = TIM4_CH1~4（PB6/PB7/PB8/PB9），默认 3kHz
 * 首次执行 pwm 命令（freq/duty/start）时才调用 hal_pwm_init() 初始化硬件；
 * 同一 TIM 的通道共用频率（ARR/PSC），占空比各自独立。
 */
typedef struct {
    hal_pwm_t    pwm;
    const char  *name;
    TIM_TypeDef *tim;
    uint8_t      channel;
    uint32_t     frequency;
} board_pwm_def_t;

static board_pwm_def_t s_pwms[] = {
    { .name = "pwm0",  .tim = TIM2, .channel = 1U, .frequency = 1000U },
    { .name = "pwm1",  .tim = TIM2, .channel = 2U, .frequency = 1000U },
    { .name = "pwm2",  .tim = TIM2, .channel = 3U, .frequency = 1000U },
    { .name = "pwm3",  .tim = TIM2, .channel = 4U, .frequency = 1000U },
    { .name = "pwm4",  .tim = TIM3, .channel = 1U, .frequency = 2000U },
    { .name = "pwm5",  .tim = TIM3, .channel = 2U, .frequency = 2000U },
    { .name = "pwm6",  .tim = TIM3, .channel = 3U, .frequency = 2000U },
    { .name = "pwm7",  .tim = TIM3, .channel = 4U, .frequency = 2000U },
    { .name = "pwm8",  .tim = TIM4, .channel = 1U, .frequency = 3000U },
    { .name = "pwm9",  .tim = TIM4, .channel = 2U, .frequency = 3000U },
    { .name = "pwm10", .tim = TIM4, .channel = 3U, .frequency = 3000U },
    { .name = "pwm11", .tim = TIM4, .channel = 4U, .frequency = 3000U },
};

void board_pwm_register(void)
{
    uint32_t i;

    for (i = 0U; i < sizeof(s_pwms) / sizeof(s_pwms[0]); i++) {
        board_pwm_def_t *def = &s_pwms[i];

        def->pwm.channel = def->channel;
        def->pwm.base.instance = def->tim;
        /* 只记录默认配置，hal_pwm_init() 推迟到命令真正使用时 */
        def->pwm.config.frequency = def->frequency;
        def->pwm.config.duty = 0U;
        def->pwm.config.polarity = HAL_PWM_POLARITY_HIGH;
        (void)device_pwm_bind(def->name, &def->pwm);
    }
}
