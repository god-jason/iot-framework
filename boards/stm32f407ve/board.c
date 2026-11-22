/*
 * board.c - STM32F407VET6 板级支持实现
 *
 * 时钟：HSE 8MHz -> PLL(M=8, N=336, P=2) -> SYSCLK 168MHz，
 *       AHB /1(168MHz)、APB1 /4(42MHz)、APB2 /2(84MHz)，FLASH 5 等待周期。
 *
 * 外设由 STM32Cube HAL 驱动（stm32_cube.h 处理了 HAL_OK 等常量与框架
 * 状态码的同名冲突）：LED 用 GPIO，USART1 用 HAL_UART + hal_uart_t 句柄。
 */

#include "board.h"

#include "ros.h"
#include "common.h"
#include "device.h"
#include "fan.h"
#include "stepper.h"
#include "stm32_cube.h"

#include <string.h>

/* USART1 的 HAL 句柄与框架句柄（全局，便于调试器查看） */
UART_HandleTypeDef huart1;

static hal_uart_t s_uart1;

/* 时钟/串口初始化失败时停机，保留现场便于调试器连接 */
static void board_halt(void)
{
    __disable_irq();
    for (;;) {
    }
}

/* ----------------------------- 时钟 ----------------------------- */

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc;
    RCC_ClkInitTypeDef clk;

    memset(&osc, 0, sizeof(osc));
    memset(&clk, 0, sizeof(clk));

    /* 168MHz 需要 Scale1 电压档 */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 8U;              /* 8MHz / 8 = 1MHz VCO 输入 */
    osc.PLL.PLLN = 336U;            /* 1MHz * 336 = 336MHz */
    osc.PLL.PLLP = RCC_PLLP_DIV2;   /* 336 / 2 = 168MHz */
    osc.PLL.PLLQ = 7U;              /* 48MHz（USB/SDIO 用，此处仅满足约束） */
    if (HAL_RCC_OscConfig(&osc) != CUBE_HAL_OK) {
        board_halt();
    }

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != CUBE_HAL_OK) {
        board_halt();
    }
}

/* ----------------------------- LED ----------------------------- */

void board_led_init(void)
{
    GPIO_InitTypeDef gpio;

    __HAL_RCC_GPIOA_CLK_ENABLE();

    memset(&gpio, 0, sizeof(gpio));
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* 低电平点亮，初始置高 = 灭 */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_SET);
}

void board_led_toggle(void)
{
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_6 | GPIO_PIN_7);
}

/* ----------------------------- UART ----------------------------- */

/* Cube UART 初始化钩子：USART1 = PA9(TX) / PA10(RX)，复用功能 AF7 */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef gpio;

    if (huart->Instance != USART1) {
        return;
    }

    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    memset(&gpio, 0, sizeof(gpio));
    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio);
}

void board_uart_init(uint32_t baudrate)
{
    hal_uart_config_t cfg;

    if (baudrate == 0U) {
        baudrate = 115200U;
    }

    memset(&s_uart1, 0, sizeof(s_uart1));
    memset(&huart1, 0, sizeof(huart1));
    huart1.Instance = USART1;
    s_uart1.base.instance = &huart1;

    memset(&cfg, 0, sizeof(cfg));
    cfg.baudrate = baudrate;
    cfg.word_length = HAL_UART_WORD_8BIT;
    cfg.parity = HAL_UART_PARITY_NONE;
    cfg.stop_bits = HAL_UART_STOP_1;
    cfg.flow_control = HAL_UART_FLOW_NONE;
    cfg.mode = HAL_UART_MODE_TX_RX;
    cfg.oversampling = false;       /* 16 倍过采样 */

    if (hal_uart_init(&s_uart1, &cfg) != HAL_OK) {
        board_halt();
    }
}

hal_uart_t *board_uart1(void)
{
    return &s_uart1;
}

hal_uart_t *board_shell_uart(void)
{
    return &s_uart1;
}

void board_trace_start(uint32_t baudrate)
{
    board_uart_init(baudrate);

    ros_trace_set_output(&s_uart1, 0U);

    /* 只输出核心事件，避免串口轮询影响实时性；需要全量时改 DEBUG */
    ros_trace_set_level(ROS_TRACE_LEVEL_CORE);
}

/* ----------------------------- PWM ----------------------------- */

static TIM_HandleTypeDef s_htim2;
static TIM_HandleTypeDef s_htim3;
static TIM_HandleTypeDef s_htim4;
static TIM_HandleTypeDef s_htim12;

typedef struct {
    hal_pwm_t          pwm;
    TIM_HandleTypeDef *htim;
    uint8_t            channel;
    uint32_t           frequency;
} board_pwm_def_t;

static board_pwm_def_t s_pwms[] = {
    { .htim = &s_htim2,  .channel = 1U, .frequency = 1000U },
    { .htim = &s_htim2,  .channel = 2U, .frequency = 1000U },
    { .htim = &s_htim2,  .channel = 3U, .frequency = 1000U },
    { .htim = &s_htim2,  .channel = 4U, .frequency = 1000U },
    { .htim = &s_htim4,  .channel = 1U, .frequency = 2000U },
    { .htim = &s_htim4,  .channel = 2U, .frequency = 2000U },
    { .htim = &s_htim4,  .channel = 3U, .frequency = 2000U },
    { .htim = &s_htim4,  .channel = 4U, .frequency = 2000U },
    { .htim = &s_htim3,  .channel = 3U, .frequency = 3000U },
    { .htim = &s_htim3,  .channel = 4U, .frequency = 3000U },
    { .htim = &s_htim12, .channel = 1U, .frequency = 4000U },
    { .htim = &s_htim12, .channel = 2U, .frequency = 4000U },
};

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

void board_pwm_register(void)
{
    uint32_t i;

    s_htim2.Instance = TIM2;
    s_htim3.Instance = TIM3;
    s_htim4.Instance = TIM4;
    s_htim12.Instance = TIM12;

    for (i = 0U; i < sizeof(s_pwms) / sizeof(s_pwms[0]); i++) {
        board_pwm_def_t *def = &s_pwms[i];

        def->pwm.channel = def->channel;
        def->pwm.base.instance = def->htim;
        def->pwm.config.frequency = def->frequency;
        def->pwm.config.duty = 0U;
        def->pwm.config.polarity = HAL_PWM_POLARITY_HIGH;
        (void)device_pwm_bind(&def->pwm);
    }
}

/* ----------------------------- 风扇 ----------------------------- */

#define FAN_PWM_SLOT  0U
#define FAN_PWM_FREQ  25000U

static fan_t s_fan;

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

void board_fan_init(void)
{
    static const fan_ops_t ops = { fan_set_duty, &s_pwms[FAN_PWM_SLOT].pwm };

    (void)fan_init(&s_fan, &ops);
}

/* ----------------------------- 步进电机 ----------------------------- */

#define STEPPER_STEP_PIN   HAL_PIN(1, 12)   /* PB12: STEP */
#define STEPPER_DIR_PIN    HAL_PIN(1, 13)   /* PB13: DIR */

static stepper_t s_stepper;

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

void board_stepper_init(void)
{
    dwt_init();
    (void)hal_gpio_config_output(STEPPER_STEP_PIN, HAL_GPIO_LOW);
    (void)hal_gpio_config_output(STEPPER_DIR_PIN, HAL_GPIO_LOW);

    if (stepper_init(&s_stepper, &s_stepper_ops) == 0) {
        (void)stepper_set_ramp(&s_stepper, 400U, 100U, 800U, 800U);
    }
}
