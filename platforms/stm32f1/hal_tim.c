/*
 * hal_tim.c - STM32F1 定时器寄存器实现（不依赖 Cube HAL）
 *
 * PWM：TIM2_CH1~4(PA0~PA3)、TIM3_CH1~4(PA6/PA7/PB0/PB1)，
 *      定时器计数时钟 72MHz（APB1 二分频后 x2），16 位分辨率。
 * 句柄约定：config.id 为 0 基定时器编号（0=TIM1，1=TIM2，…），
 *      config.channel = 1..4；hal_pwm_init() 按编号解析定时器实例。
 *
 * 无 Cube HAL 的构建不提供硬件定时器/中断加减速：hal_tim_* 仅为满足链接，
 * 运行时返回 HAL_NOT_SUPPORTED（步进电机请使用软件延时）。
 */

#if defined(ROS_STM32_F1)

#include "hal.h"
#include "stm32f10x.h"

#define STM32_RCC_APB1ENR_TIM2EN   (1UL << 0)
#define STM32_RCC_APB1ENR_TIM3EN   (1UL << 1)
#define STM32_RCC_APB1ENR_TIM4EN   (1UL << 2)
#define STM32_RCC_APB1ENR_TIM5EN   (1UL << 3)

typedef struct {
    void    *tim;
    uint8_t  channel;           /* 1..4 */
    uint8_t  port;
    uint8_t  pin;
} stm32_pwm_map_t;

/*
 * STM32F103 定时器 PWM 通道与默认引脚（未做重映射）：
 *   TIM2  CH1=PA0  CH2=PA1  CH3=PA2  CH4=PA3    重映射：PA15/PB3/PB10/PB11
 *   TIM3  CH1=PA6  CH2=PA7  CH3=PB0  CH4=PB1    重映射：PC6/PC7/PC8/PC9
 *   TIM4  CH1=PB6  CH2=PB7  CH3=PB8  CH4=PB9    重映射：PD12/PD13/PD14/PD15
 *   TIM5  CH1=PA0  CH2=PA1  CH3=PA2  CH4=PA3    重映射：PH10/PH11/PH12/PH13
 *         （TIM5 仅大容量 F103 有；与 TIM2 默认通道共用 PA0~PA3，不能同时使用）
 *   另：PA9/PA10 已用于 USART1，PA13/PA14 为 SWD，请勿占用。
 */
static const stm32_pwm_map_t s_stm32_pwm[] = {
    /* TIM2：PA0 / PA1 / PA2 / PA3 */
    { TIM2, 1U, 0U, 0U }, { TIM2, 2U, 0U, 1U },
    { TIM2, 3U, 0U, 2U }, { TIM2, 4U, 0U, 3U },
    /* TIM3：PA6 / PA7 / PB0 / PB1 */
    { TIM3, 1U, 0U, 6U }, { TIM3, 2U, 0U, 7U },
    { TIM3, 3U, 1U, 0U }, { TIM3, 4U, 1U, 1U },
#ifdef TIM4
    /* TIM4：PB6 / PB7 / PB8 / PB9 */
    { TIM4, 1U, 1U, 6U }, { TIM4, 2U, 1U, 7U },
    { TIM4, 3U, 1U, 8U }, { TIM4, 4U, 1U, 9U },
#endif
#ifdef TIM5
    /* TIM5：PA0 / PA1 / PA2 / PA3（与 TIM2 冲突，仅大容量芯片） */
    { TIM5, 1U, 0U, 0U }, { TIM5, 2U, 0U, 1U },
    { TIM5, 3U, 0U, 2U }, { TIM5, 4U, 0U, 3U },
#endif
};

#define STM32_PWM_CLOCK_HZ  72000000UL     /* APB1 x2 */
#define STM32_PWM_PSC       71UL           /* /72 -> 1MHz */

/* 定时器编号：0=TIM1，1=TIM2，2=TIM3，3=TIM4，4=TIM5，5=TIM8 */
static TIM_TypeDef *const s_stm32_tim_inst[] = {
    [0] = TIM1,
    [1] = TIM2,
#ifdef TIM3
    [2] = TIM3,
#endif
#ifdef TIM4
    [3] = TIM4,
#endif
#ifdef TIM5
    [4] = TIM5,
#endif
#ifdef TIM8
    [5] = TIM8,
#endif
};

#define STM32_TIM_CH_COUNT \
    (sizeof(s_stm32_tim_inst) / sizeof(s_stm32_tim_inst[0]))

/* 定时器是否已配置过时基：同组通道初始化时不再重置 ARR/PSC，避免打断运行 */
static uint8_t s_stm32_tim_ready[4];       /* TIM2 / TIM3 / TIM4 / TIM5 */

static uint32_t stm32_tim_slot(TIM_TypeDef *tim)
{
    if (tim == TIM2) {
        return 0U;
    }
#ifdef TIM3
    if (tim == TIM3) {
        return 1U;
    }
#endif
#ifdef TIM4
    if (tim == TIM4) {
        return 2U;
    }
#endif
#ifdef TIM5
    if (tim == TIM5) {
        return 3U;
    }
#endif
    return 0xFFU;
}

static const stm32_pwm_map_t *stm32_pwm_find(const hal_pwm_t *handle)
{
    uint32_t i;

    for (i = 0U; i < sizeof(s_stm32_pwm) / sizeof(s_stm32_pwm[0]); i++) {
        if (s_stm32_pwm[i].tim == handle->base.instance &&
            s_stm32_pwm[i].channel == handle->config.channel) {
            return &s_stm32_pwm[i];
        }
    }
    return NULL;
}

static uint32_t stm32_pwm_arr(uint32_t freq)
{
    uint32_t ticks = (STM32_PWM_CLOCK_HZ / (STM32_PWM_PSC + 1U)) / freq;

    if (ticks == 0U) {
        ticks = 1U;
    }
    if (ticks > 65536U) {
        ticks = 65536U;
    }
    return ticks - 1U;
}

static uint32_t stm32_pwm_ccr(uint32_t arr, uint32_t duty)
{
    if (duty > 10000U) {
        duty = 10000U;
    }
    return ((arr + 1U) * duty) / 10000U;
}

static void stm32_pwm_apply(hal_pwm_t *handle, TIM_TypeDef *tim)
{
    uint32_t ccr = stm32_pwm_ccr(tim->ARR, handle->config.duty);
    uint32_t pol = (handle->config.polarity == HAL_PWM_POLARITY_LOW) ? 1U : 0U;

    switch (handle->config.channel) {
    case 1U:
        tim->CCMR1 = (tim->CCMR1 & ~0x00FFUL) | 0x0068UL;
        tim->CCR1 = ccr;
        tim->CCER = (tim->CCER & ~0x0003UL) | 0x0001UL | (pol << 1);
        break;
    case 2U:
        tim->CCMR1 = (tim->CCMR1 & ~0xFF00UL) | 0x6800UL;
        tim->CCR2 = ccr;
        tim->CCER = (tim->CCER & ~0x0030UL) | 0x0010UL | (pol << 5);
        break;
    case 3U:
        tim->CCMR2 = (tim->CCMR2 & ~0x00FFUL) | 0x0068UL;
        tim->CCR3 = ccr;
        tim->CCER = (tim->CCER & ~0x0300UL) | 0x0100UL | (pol << 9);
        break;
    default:
        tim->CCMR2 = (tim->CCMR2 & ~0xFF00UL) | 0x6800UL;
        tim->CCR4 = ccr;
        tim->CCER = (tim->CCER & ~0x3000UL) | 0x1000UL | (pol << 13);
        break;
    }
}

int hal_pwm_init(hal_pwm_t *handle, const hal_pwm_config_t *config)
{
    const stm32_pwm_map_t *map;
    TIM_TypeDef *tim;
    hal_gpio_config_t gpio;
    uint32_t slot;

    if (handle == NULL || config == NULL || config->frequency == 0U) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    if (config->id >= STM32_TIM_CH_COUNT) {
        return HAL_INVALID_PARAM;
    }
    tim = s_stm32_tim_inst[config->id];
    if (tim == NULL) {
        return HAL_INVALID_PARAM;
    }
    handle->base.instance = tim;
    handle->config = *config;
    map = stm32_pwm_find(handle);
    if (map == NULL) {
        handle->base.instance = NULL;
        return HAL_INVALID_PARAM;
    }

    /* 使能对应定时器时钟（TIM2~TIM5 均在 APB1） */
    if (tim == TIM2) {
        RCC->APB1ENR |= STM32_RCC_APB1ENR_TIM2EN;
#ifdef TIM3
    } else if (tim == TIM3) {
        RCC->APB1ENR |= STM32_RCC_APB1ENR_TIM3EN;
#endif
#ifdef TIM4
    } else if (tim == TIM4) {
        RCC->APB1ENR |= STM32_RCC_APB1ENR_TIM4EN;
#endif
#ifdef TIM5
    } else if (tim == TIM5) {
        RCC->APB1ENR |= STM32_RCC_APB1ENR_TIM5EN;
#endif
    }
    gpio.mode = HAL_GPIO_MODE_AF;
    gpio.output = HAL_GPIO_OUTPUT_PP;
    gpio.pull = HAL_GPIO_NOPULL;
    gpio.speed = HAL_GPIO_SPEED_HIGH;
    gpio.alternate = 0U;
    gpio.initial_level = HAL_GPIO_LOW;
    (void)hal_gpio_init(HAL_PIN(map->port, map->pin), &gpio);

    if (handle->config.duty > 10000U) {
        handle->config.duty = 10000U;
    }

    slot = stm32_tim_slot(tim);
    if (slot >= 4U) {
        return HAL_INVALID_PARAM;
    }
    if (s_stm32_tim_ready[slot] == 0U) {
        /* 该定时器首次被某个通道使用：配置时基 */
        tim->CR1 = 0U;
        tim->PSC = STM32_PWM_PSC;
        tim->ARR = stm32_pwm_arr(config->frequency);
        tim->CNT = 0U;
        tim->EGR = 1U;                              /* UG：装载 PSC/ARR */
        tim->SR = 0U;
        s_stm32_tim_ready[slot] = 1U;
        tim->CR1 = 0x0080U;                         /* ARPE，等待 start */
    } else {
        /* 同组已有通道初始化过：沿用现有 PSC/ARR，不打断正在运行的通道 */
        handle->config.frequency =
            (STM32_PWM_CLOCK_HZ / (STM32_PWM_PSC + 1U)) / (tim->ARR + 1U);
    }

    stm32_pwm_apply(handle, tim);
    handle->base.error = HAL_OK;
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

int hal_pwm_deinit(hal_pwm_t *handle)
{
    const stm32_pwm_map_t *map;
    TIM_TypeDef *tim;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_OK;
    }
    tim = (TIM_TypeDef *)handle->base.instance;
    tim->CR1 = 0U;
    tim->CCER = 0U;
    map = stm32_pwm_find(handle);
    if (map != NULL) {
        (void)hal_gpio_deinit(HAL_PIN(map->port, map->pin));
    }
    handle->base.state = HAL_STATE_RESET;
    return HAL_OK;
}

int hal_pwm_start(hal_pwm_t *handle)
{
    TIM_TypeDef *tim;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_READY &&
        handle->base.state != HAL_STATE_BUSY) {
        return HAL_NOT_READY;
    }
    tim = (TIM_TypeDef *)handle->base.instance;
    tim->CR1 |= 0x0001U;                            /* CEN（同一定时器共用） */
    stm32_pwm_apply(handle, tim);                   /* 恢复本通道占空比 */
    handle->base.state = HAL_STATE_BUSY;
    return HAL_OK;
}

int hal_pwm_stop(hal_pwm_t *handle)
{
    TIM_TypeDef *tim;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    tim = (TIM_TypeDef *)handle->base.instance;
    /* 只清本通道 CCR，保持同一定时器的其它通道继续输出 */
    switch (handle->config.channel) {
    case 1U: tim->CCR1 = 0U; break;
    case 2U: tim->CCR2 = 0U; break;
    case 3U: tim->CCR3 = 0U; break;
    default: tim->CCR4 = 0U; break;
    }
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

int hal_pwm_set_frequency(hal_pwm_t *handle, uint32_t frequency)
{
    TIM_TypeDef *tim;

    if (handle == NULL || frequency == 0U) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    tim = (TIM_TypeDef *)handle->base.instance;
    handle->config.frequency = frequency;
    tim->ARR = stm32_pwm_arr(frequency);
    tim->EGR = 1U;
    stm32_pwm_apply(handle, tim);
    return HAL_OK;
}

int hal_pwm_set_duty(hal_pwm_t *handle, uint32_t duty)
{
    TIM_TypeDef *tim;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    if (duty > 10000U) {
        duty = 10000U;
    }
    tim = (TIM_TypeDef *)handle->base.instance;
    handle->config.duty = duty;
    stm32_pwm_apply(handle, tim);
    return HAL_OK;
}

int hal_pwm_set_polarity(hal_pwm_t *handle, hal_pwm_polarity_e polarity)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    handle->config.polarity = polarity;
    stm32_pwm_apply(handle, (TIM_TypeDef *)handle->base.instance);
    return HAL_OK;
}

int hal_tim_init(hal_tim_t *handle, const hal_tim_config_t *config)
{
    (void)handle;
    (void)config;
    return HAL_NOT_SUPPORTED;
}

int hal_tim_deinit(hal_tim_t *handle)
{
    (void)handle;
    return HAL_NOT_SUPPORTED;
}

int hal_tim_start(hal_tim_t *handle)
{
    (void)handle;
    return HAL_NOT_SUPPORTED;
}

int hal_tim_stop(hal_tim_t *handle)
{
    (void)handle;
    return HAL_NOT_SUPPORTED;
}

int hal_tim_start_it(hal_tim_t *handle)
{
    (void)handle;
    return HAL_NOT_SUPPORTED;
}

int hal_tim_stop_it(hal_tim_t *handle)
{
    (void)handle;
    return HAL_NOT_SUPPORTED;
}

int hal_tim_set_period(hal_tim_t *handle, uint32_t period)
{
    (void)handle;
    (void)period;
    return HAL_NOT_SUPPORTED;
}

int hal_tim_set_counter(hal_tim_t *handle, uint32_t counter)
{
    (void)handle;
    (void)counter;
    return HAL_NOT_SUPPORTED;
}

#endif /* ROS_STM32_F1 */
