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
