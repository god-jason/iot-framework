/*
 * app_device.c - STM32F407VET6 应用级设备初始化
 *
 * 负责：
 *   - 板载 LED（PA6/PA7，低电平点亮）；
 *   - PWM 通道注册（TIM2/TIM3/TIM4/TIM12）；
 *   - 风扇调速（复用 PWM 0）；
 *   - 步进电机（PB12 STEP / PB13 DIR，DWT 微秒延时）；
 *   - 1Hz 心跳任务。
 */

#include "app_device.h"

#include "board.h"
#include "ros.h"
#include "device.h"
#include "fan.h"
#include "stepper.h"
#include "hal_gpio.h"
#include "hal_pwm.h"

#include "stm32_cube.h"

#include <string.h>

/* ==========================================================================
 * PWM 注册
 * ========================================================================== */

/* instance 为定时器编号（0=TIM1，1=TIM2，2=TIM3，3=TIM4，11=TIM12） */
typedef struct {
    hal_pwm_t pwm;
    uint8_t   instance;
    uint8_t   channel;
    uint32_t  frequency;
} app_pwm_def_t;

static app_pwm_def_t s_pwms[] = {
    { .instance = 1U,  .channel = 1U, .frequency = 1000U },
    { .instance = 1U,  .channel = 2U, .frequency = 1000U },
    { .instance = 1U,  .channel = 3U, .frequency = 1000U },
    { .instance = 1U,  .channel = 4U, .frequency = 1000U },
    { .instance = 3U,  .channel = 1U, .frequency = 2000U },
    { .instance = 3U,  .channel = 2U, .frequency = 2000U },
    { .instance = 3U,  .channel = 3U, .frequency = 2000U },
    { .instance = 3U,  .channel = 4U, .frequency = 2000U },
    { .instance = 2U,  .channel = 3U, .frequency = 3000U },
    { .instance = 2U,  .channel = 4U, .frequency = 3000U },
    { .instance = 11U, .channel = 1U, .frequency = 4000U },
    { .instance = 11U, .channel = 2U, .frequency = 4000U },
};

/* 配置一组复用引脚为推挽输出 */
static void pwm_gpio_init(GPIO_TypeDef *port, uint16_t pins, uint32_t af)
{
    GPIO_InitTypeDef gpio;

    memset(&gpio, 0, sizeof(gpio));
    gpio.Pin = pins;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = af;
    HAL_GPIO_Init(port, &gpio);
}

/* Cube PWM 初始化钩子：按定时器使能时钟并配置默认 PWM 引脚复用 */
void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2) {
        __HAL_RCC_TIM2_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        pwm_gpio_init(GPIOA, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3,
                      GPIO_AF1_TIM2);
    } else if (htim->Instance == TIM3) {
        __HAL_RCC_TIM3_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        pwm_gpio_init(GPIOB, GPIO_PIN_0 | GPIO_PIN_1, GPIO_AF2_TIM3);
    } else if (htim->Instance == TIM4) {
        __HAL_RCC_TIM4_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        pwm_gpio_init(GPIOB, GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9,
                      GPIO_AF2_TIM4);
    } else if (htim->Instance == TIM12) {
        __HAL_RCC_TIM12_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        pwm_gpio_init(GPIOB, GPIO_PIN_14 | GPIO_PIN_15, GPIO_AF9_TIM12);
    }
}

/* 向 device 组件注册本板 PWM 通道（只绑定句柄，硬件初始化延迟到首次使用） */
static void app_pwm_register(void)
{
    uint32_t i;

    for (i = 0U; i < sizeof(s_pwms) / sizeof(s_pwms[0]); i++) {
        app_pwm_def_t *def = &s_pwms[i];

        def->pwm.config.channel = def->channel;
        def->pwm.config.id = def->instance;
        def->pwm.config.frequency = def->frequency;
        def->pwm.config.duty = 0U;
        def->pwm.config.polarity = HAL_PWM_POLARITY_HIGH;
        (void)device_pwm_bind(&def->pwm);
    }
}

/* ==========================================================================
 * 风扇
 * ========================================================================== */

/* 风扇：复用 PWM 编号 0（TIM2_CH1 / PA0）输出，25kHz */
#define FAN_PWM_SLOT  0U
#define FAN_PWM_FREQ  25000U

static fan_t s_fan;

/* fan 组件的占空比回调：0 停止，1~1000 映射到 0.1%~100% */
static int fan_set_duty(void *ctx, uint16_t duty)
{
    hal_pwm_t *pwm = (hal_pwm_t *)ctx;

    if (duty == 0U) {
        if (pwm->base.state == HAL_STATE_RESET) {
            return 0;
        }
        (void)hal_pwm_set_duty(pwm, 0U);
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

/* ==========================================================================
 * 步进电机
 * ========================================================================== */

/* 步进电机：STEP=PB12，DIR=PB13 */
#define STEPPER_STEP_PIN   HAL_PIN(1, 12)   /* PB12: STEP */
#define STEPPER_DIR_PIN    HAL_PIN(1, 13)   /* PB13: DIR */

static stepper_t s_stepper;

/* 使能 DWT 周期计数器，用于微秒级延时 */
static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* 基于 DWT 的忙等微秒延时 */
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
    dwt_delay_us(2U);
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
        (void)stepper_set_ramp(&s_stepper, 400U, 100U, 800U, 800U);
    }
}

/* ==========================================================================
 * 板载 LED 与心跳
 * ========================================================================== */

/* 板载 LED（PA6 / PA7，低电平点亮） */
static void app_led_init(void)
{
    GPIO_InitTypeDef gpio;

    __HAL_RCC_GPIOA_CLK_ENABLE();

    memset(&gpio, 0, sizeof(gpio));
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_SET);
}

static void app_led_toggle(void)
{
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_6 | GPIO_PIN_7);
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

/* ==========================================================================
 * 对外入口
 * ========================================================================== */

/*
 * 应用设备初始化：
 *   1. LED 输出；
 *   2. device 组件（gpio/pwm 命令）与 PWM/风扇注册；
 *   3. 步进电机；
 *   4. 心跳任务。
 */
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
