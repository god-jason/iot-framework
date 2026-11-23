/*
 * app_device.c - STM32F103CBT6 应用级设备初始化
 *
 * 板载 LED（PC13）、PWM/风扇/步进注册与 1Hz 心跳任务。
 * 纯寄存器实现，不依赖 STM32Cube HAL。
 */

#include "app_device.h"

#include "board.h"
#include "ros.h"
#include "device.h"
#include "fan.h"
#include "stepper.h"

#include "stm32f10x.h"

/* ==========================================================================
 * PWM / 风扇 / 步进
 * ========================================================================== */

/* 向 device 组件注册本板 PWM 通道（只绑定句柄，不做硬件初始化）
 *   编号 0~3   = TIM2_CH1~4（PA0~PA3），默认 1kHz
 *   编号 4~7   = TIM3_CH1~4（PA6/PA7/PB0/PB1），默认 2kHz
 *   编号 8~11  = TIM4_CH1~4（PB6/PB7/PB8/PB9），默认 3kHz
 * 首次执行 pwm 命令（freq/duty/start）时才调用 hal_pwm_init() 初始化硬件；
 * 同一 TIM 的通道共用频率（ARR/PSC），占空比各自独立。
 * instance 为定时器编号（0=TIM1，1=TIM2，2=TIM3，3=TIM4）。
 */
typedef struct {
    hal_pwm_t   pwm;
    uint8_t     instance;
    uint8_t     channel;
    uint32_t    frequency;
} app_pwm_def_t;

static app_pwm_def_t s_pwms[] = {
    { .instance = 1U, .channel = 1U, .frequency = 1000U },
    { .instance = 1U, .channel = 2U, .frequency = 1000U },
    { .instance = 1U, .channel = 3U, .frequency = 1000U },
    { .instance = 1U, .channel = 4U, .frequency = 1000U },
    { .instance = 2U, .channel = 1U, .frequency = 2000U },
    { .instance = 2U, .channel = 2U, .frequency = 2000U },
    { .instance = 2U, .channel = 3U, .frequency = 2000U },
    { .instance = 2U, .channel = 4U, .frequency = 2000U },
    { .instance = 3U, .channel = 1U, .frequency = 3000U },
    { .instance = 3U, .channel = 2U, .frequency = 3000U },
    { .instance = 3U, .channel = 3U, .frequency = 3000U },
    { .instance = 3U, .channel = 4U, .frequency = 3000U },
};

static void app_pwm_register(void)
{
    uint32_t i;

    for (i = 0U; i < sizeof(s_pwms) / sizeof(s_pwms[0]); i++) {
        app_pwm_def_t *def = &s_pwms[i];

        def->pwm.config.channel = def->channel;
        /* 只记录默认配置，hal_pwm_init() 推迟到命令真正使用时 */
        def->pwm.config.id = def->instance;
        def->pwm.config.frequency = def->frequency;
        def->pwm.config.duty = 0U;
        def->pwm.config.polarity = HAL_PWM_POLARITY_HIGH;
        (void)device_pwm_bind(&def->pwm);
    }
}

/* 风扇调速复用编号 0（TIM2_CH1 / PA0），25kHz 为常见 4 线风扇 PWM 频率。
 * 档位 0 停止输出；1~1000 映射到 0.1%~100% 占空比。
 * 注意：该通道同时暴露为 PWM 编号 0，用 pwm 命令改它会影响风扇输出。 */
#define FAN_PWM_SLOT  0U
#define FAN_PWM_FREQ  25000U

static fan_t s_fan;

static int fan_set_duty(void *ctx, uint16_t duty)
{
    hal_pwm_t *pwm = (hal_pwm_t *)ctx;

    if (duty == 0U) {
        if (pwm->base.state == HAL_STATE_RESET) {
            return 0;                               /* 未初始化：无需停止 */
        }
        (void)hal_pwm_set_duty(pwm, 0U);            /* 同步配置里的占空比 */
        return (hal_pwm_stop(pwm) == HAL_OK) ? 0 : -1;
    }
    if (pwm->base.state == HAL_STATE_RESET) {
        pwm->config.frequency = FAN_PWM_FREQ;
        pwm->config.duty = 0U;
        if (hal_pwm_init(pwm, &pwm->config) != HAL_OK) {
            return -1;
        }
    }
    if (hal_pwm_set_duty(pwm, (uint32_t)duty * 10U) != HAL_OK) {
        return -1;
    }
    return (hal_pwm_start(pwm) == HAL_OK) ? 0 : -1;
}

static void app_fan_init(void)
{
    static const fan_ops_t ops = { fan_set_duty, &s_pwms[FAN_PWM_SLOT].pwm };

    (void)fan_init(&s_fan, &ops);
}

/* 步进电机：STEP=PA4，DIR=PA5 */
#define STEPPER_STEP_PIN   HAL_PIN(0, 4)    /* PA4: STEP 脉冲 */
#define STEPPER_DIR_PIN    HAL_PIN(0, 5)    /* PA5: DIR 方向 */

static stepper_t s_stepper;

/* DWT 周期计数器用于微秒延时（SysTick 已被内核占用） */
static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void dwt_delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000U);

    while ((DWT->CYCCNT - start) < cycles) {
    }
}

static int stepper_set_dir(void *ctx, uint8_t dir)
{
    (void)ctx;
    return (hal_gpio_write(STEPPER_DIR_PIN,
                           (dir != 0U) ? HAL_GPIO_HIGH : HAL_GPIO_LOW) == HAL_OK)
               ? 0 : -1;
}

static void stepper_delay_us(void *ctx, uint32_t us)
{
    (void)ctx;
    dwt_delay_us(us);
}

static int stepper_step(void *ctx)
{
    (void)ctx;
    (void)hal_gpio_write(STEPPER_STEP_PIN, HAL_GPIO_HIGH);
    dwt_delay_us(2U);                               /* 2us 脉冲宽度 */
    (void)hal_gpio_write(STEPPER_STEP_PIN, HAL_GPIO_LOW);
    return 0;
}

static const stepper_ops_t s_stepper_ops = {
    .set_dir = stepper_set_dir,
    .step = stepper_step,
    .delay_us = stepper_delay_us,
    .ctx = NULL
};

static void app_stepper_init(void)
{
    dwt_init();
    (void)hal_gpio_config_output(STEPPER_STEP_PIN, HAL_GPIO_LOW);
    (void)hal_gpio_config_output(STEPPER_DIR_PIN, HAL_GPIO_LOW);

    if (stepper_init(&s_stepper, &s_stepper_ops) == 0) {
        /* 默认加减速：最高 400 步/s，起停 100，加速度 800 步/s² */
        (void)stepper_set_ramp(&s_stepper, 400U, 100U, 800U, 800U);
    }
}

/* ==========================================================================
 * 板载 LED 与心跳任务
 * ========================================================================== */

/* 板载 LED（PC13，低电平点亮） */
#define APP_LED_PIN  13U

static void app_led_init(void)
{
    RCC->APB2ENR |= (1UL << 4);     /* IOPCEN */

    /* PC13 属于 CRH，位段 [23:20]：MODE=10(2MHz 输出) CNF=00(推挽) */
    GPIOC->CRH &= ~(0x0FUL << ((APP_LED_PIN - 8U) * 4U));
    GPIOC->CRH |=  (0x02UL << ((APP_LED_PIN - 8U) * 4U));

    /* 低电平点亮，初始置高 = 灭 */
    GPIOC->BSRR = (1UL << APP_LED_PIN);
}

static void app_led_toggle(void)
{
    GPIOC->ODR ^= (1UL << APP_LED_PIN);
}

/* 心跳任务：板载 LED 1Hz 闪烁 */
static ros_task_t s_blink_task;
static uint8_t    s_blink_stack[BOARD_BLINK_STACK_SIZE];

static void blink_entry(void *arg)
{
    (void)arg;

    for (;;) {
        app_led_toggle();
        ros_sleep_ms(500U);
    }
}

void app_device_init(void)
{
    app_led_init();

    /* 板级设备：gpio/pwm 命令 + PWM/风扇/步进 */
    if (device_init() == 0) {
        app_pwm_register();
        app_fan_init();
    }
    app_stepper_init();

    /* 心跳：板载 LED 1Hz */
    (void)ros_task_create(&s_blink_task, "blink", 2U, blink_entry, NULL,
                          s_blink_stack, sizeof(s_blink_stack));
}
