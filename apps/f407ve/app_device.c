/*
 * app_device.c - STM32F407VET6 应用级设备初始化
 *
 * 负责：
 *   - 指示灯 PA1（低电平点亮）；
 *   - 风扇调速（TIM2_CH1 / PA0，25kHz）；
 *   - 步进电机（PB12 STEP / PB13 DIR，DWT 微秒延时）；
 *   - 1Hz 心跳任务。
 */

#include "app_device.h"

#include "board.h"
#include "ros.h"
#include "gpio.h"
#include "fan.h"
#include "stepper.h"
#include "hal_gpio.h"
#include "hal_pwm.h"

#include "stm32_cube.h"

#include <string.h>

/* ==========================================================================
 * PWM 引脚复用
 * ========================================================================== */

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
        /* 只配 TIM2_CH1（PA0，风扇）。PA1 为指示灯、PA2/PA3 为 USART2，
         * 不能再被复用成 PWM 通道。 */
        pwm_gpio_init(GPIOA, GPIO_PIN_0, GPIO_AF1_TIM2);
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

/* ==========================================================================
 * 风扇
 * ========================================================================== */

/* 风扇：TIM2_CH1 / PA0，25kHz */
#define FAN_PWM_FREQ  25000U

static hal_pwm_t s_fan_pwm;
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
    static const fan_ops_t ops = { fan_set_duty, &s_fan_pwm };

    /* 只记录默认配置，硬件初始化延迟到 fan 首次设置转速时 */
    s_fan_pwm.config.id = 1U;                     /* TIM2 */
    s_fan_pwm.config.channel = 1U;
    s_fan_pwm.config.frequency = 1000U;
    s_fan_pwm.config.duty = 0U;
    s_fan_pwm.config.polarity = HAL_PWM_POLARITY_HIGH;

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

/* 指示灯（PA1，低电平点亮） */
static void app_led_init(void)
{
    GPIO_InitTypeDef gpio;

    __HAL_RCC_GPIOA_CLK_ENABLE();

    memset(&gpio, 0, sizeof(gpio));
    gpio.Pin = GPIO_PIN_1;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);   /* 熄灭 */
}

static void app_led_toggle(void)
{
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_1);
}

/* 心跳任务：PA1 指示灯每 500ms 翻转一次（1 秒一个闪烁周期） */
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
 *   2. gpio 命令与风扇注册；
 *   3. 步进电机；
 *   4. 心跳任务。
 */
void app_device_init(void)
{
    app_led_init();

    /* 板级设备：gpio 命令 + 风扇/步进 */
    (void)gpio_cmd_register();
    app_fan_init();
    app_stepper_init();

    /* 心跳：板载 LED 1Hz */
    (void)ros_task_create(&s_blink_task, "blink", 2U, blink_entry, NULL,
                          s_blink_stack, sizeof(s_blink_stack));
}
