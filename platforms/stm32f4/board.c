/*
 * board.c - STM32F407VET6 板级支持实现
 *
 * 时钟：HSE 8MHz -> PLL(M=8, N=336, P=2) -> SYSCLK 168MHz，
 *       AHB /1(168MHz)、APB1 /4(42MHz)、APB2 /2(84MHz)，FLASH 5 等待周期。
 *
 * 外设由 STM32Cube HAL 驱动（stm32_cube.h 处理了 HAL_OK 等常量与框架
 * 状态码的同名冲突）：LED 用 GPIO，USART1 用 HAL_UART + hal_uart_t 句柄。
 *
 * 应用级设备注册（PWM / 风扇 / 步进）见 apps/<name>/app_main.c。
 */

#include "board.h"

#include "ros.h"
#include "stm32_cube.h"

#include <string.h>

/* USART1 / USART2 的框架句柄（全局，便于调试器查看） */
static hal_uart_t s_uart1;
static hal_uart_t s_uart2;

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

/* ----------------------------- UART ----------------------------- */

/* Cube UART 初始化钩子：USART1 = PA9/PA10，USART2 = PA2/PA3，复用功能 AF7 */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef gpio;

    if (huart->Instance == USART1) {
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        memset(&gpio, 0, sizeof(gpio));
        gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10;
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Pull = GPIO_PULLUP;
        gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio.Alternate = GPIO_AF7_USART1;
        HAL_GPIO_Init(GPIOA, &gpio);
    } else if (huart->Instance == USART2) {
        __HAL_RCC_USART2_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        memset(&gpio, 0, sizeof(gpio));
        gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3;
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Pull = GPIO_PULLUP;
        gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio.Alternate = GPIO_AF7_USART2;
        HAL_GPIO_Init(GPIOA, &gpio);
    }
}

/* 初始化一个串口句柄（instance：0 = USART1，1 = USART2，平台按编号定位实例） */
static void board_uart_setup(hal_uart_t *handle, uint8_t instance,
                             uint32_t baudrate)
{
    hal_uart_config_t cfg;

    memset(handle, 0, sizeof(*handle));

    memset(&cfg, 0, sizeof(cfg));
    cfg.baudrate = (baudrate != 0U) ? baudrate : 115200U;
    cfg.word_length = HAL_UART_WORD_8BIT;
    cfg.parity = HAL_UART_PARITY_NONE;
    cfg.stop_bits = HAL_UART_STOP_1;
    cfg.flow_control = HAL_UART_FLOW_NONE;
    cfg.mode = HAL_UART_MODE_TX_RX;
    cfg.oversampling = false;       /* 16 倍过采样 */
    cfg.id = instance;

    if (hal_uart_init(handle, &cfg) != HAL_OK) {
        board_halt();
    }
}

void board_uart_init(uint32_t baudrate)
{
    board_uart_setup(&s_uart1, 0U, baudrate);
}

hal_uart_t *board_uart1(void)
{
    return &s_uart1;
}

/* USART2：ML307 模组 AT 通道，首次获取句柄时初始化 */
hal_uart_t *board_uart2(void)
{
    static bool inited;

    if (!inited) {
        board_uart_setup(&s_uart2, 1U, 115200U);
        inited = true;
    }
    return &s_uart2;
}

hal_uart_t *board_shell_uart(void)
{
    return &s_uart1;
}

void board_trace_start(uint32_t baudrate)
{
    board_uart_init(baudrate);

    /*
     * trace 与 Shell/printf 共用 UART1：CORE 级事件由调度器/中断上下文
     * 高频产生（每个事件一行，行首固定为 'T'），而 hal_uart_transmit
     * 是轮询发送且不可重入，两个上下文同时发送会互相覆盖 HAL 的
     * pTxBuffPtr/TxXferCount，把栈内容当成待发数据打出去。
     * 因此默认只注册输出通道、不打开 trace；需要时再显式
     * ros_trace_set_level() 打开（注意此时应避开日志输出）。
     */
    ros_trace_set_output(&s_uart1, 50U);
    ros_trace_set_level(ROS_TRACE_LEVEL_OFF);
}

/* ==========================================================================
 * 板级早期初始化（由 platforms/stm32/stm32_main.c 的 main 调用）
 * ========================================================================== */

void board_init(void)
{
    HAL_Init();                 /* SysTick 1ms、NVIC 分组、FLASH 预取 */
    SystemClock_Config();       /* HSE 8MHz -> 168MHz */
}
