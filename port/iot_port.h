#ifndef IOT_PORT_H
#define IOT_PORT_H

/*
 * Port 平台接口层（优化版）。
 * 框架 Core / Components 不直接依赖 STM32 HAL 或具体寄存器，
 * 而是通过本文件的统一接口访问底层能力。
 * 具体实现由 mcu/simulator（host）、mcu/stm32f1、mcu/stm32f4 提供。
 *
 * 约定：
 *  - 所有操作函数返回 int：0 = 成功（IOT_OK），负值 = iot_err_t 错误码。
 *  - 每个外设实例为「操作函数集 + void *ctx」；ctx 由实现层自定义，
 *    各操作函数的第一个参数即该 ctx。
 *  - 外设 init 统一接收 const 配置结构体（参考 STM32 HAL 的 InitTypeDef），
 *    其中 instance 字段为外设编号（USART1/SPI1/I2C1/TIM1/ADC1...）。
 *  - 外设通过 iot_port_register_*（按名）注册，用例通过 iot_port_get_*（按名）获取。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================= 系统基础 ================= */
typedef struct {
    void (*init)(void);

    iot_tick_t (*tick_get)(void);     /* 单调递增 tick（通常 1ms） */
    void (*delay_ms)(uint32_t ms);
    void (*delay_us)(uint32_t us);

    void (*yield)(void);              /* 让出 CPU */

    void (*critical_enter)(void);     /* 进入临界区（关中断） */
    void (*critical_exit)(void);      /* 退出临界区 */

    void (*reset)(void);              /* 系统复位 */
} iot_port_system_t;

/* ================= UART ================= */
/* 参考 STM32 UART_InitTypeDef：字长 / 校验 / 停止位与波特率 */
typedef enum {
    IOT_UART_WORD_8B = 0x00,
    IOT_UART_WORD_9B = 0x01
} iot_uart_word_len_t;

typedef enum {
    IOT_UART_PARITY_NONE = 0x00,
    IOT_UART_PARITY_EVEN = 0x01,
    IOT_UART_PARITY_ODD  = 0x02
} iot_uart_parity_t;

typedef enum {
    IOT_UART_STOP_1 = 0x00,
    IOT_UART_STOP_2 = 0x01
} iot_uart_stop_t;

typedef struct {
    uint32_t instance;               /* 编号：USART1 / USART2 / ... */
    uint32_t baudrate;
    iot_uart_word_len_t word_len;
    iot_uart_parity_t parity;
    iot_uart_stop_t stop_bits;
} iot_uart_config_t;

typedef struct {
    int (*init)(void *ctx, const iot_uart_config_t *config);
    int (*deinit)(void *ctx);

    int (*send)(void *ctx, const uint8_t *data, uint32_t len);
    int (*receive)(void *ctx, uint8_t *data, uint32_t len);

    int (*set_baudrate)(void *ctx, uint32_t baudrate);
    int (*flush)(void *ctx);

    uint32_t (*available)(void *ctx);   /* 可读字节数（RingBuffer/DMA 场景） */

    void *ctx;
} iot_port_uart_t;

/* ================= GPIO ================= */
typedef enum {
    IOT_GPIO_INPUT          = 0,
    IOT_GPIO_OUTPUT         = 1,
    IOT_GPIO_INPUT_PULLUP   = 2,
    IOT_GPIO_INPUT_PULLDOWN = 3,
    IOT_GPIO_OUTPUT_OD      = 4
} iot_gpio_mode_t;

typedef enum {
    IOT_GPIO_LOW  = 0,
    IOT_GPIO_HIGH = 1
} iot_gpio_level_t;

/* 参考 STM32 GPIO：端口 + 引脚编号 + 模式 */
typedef struct {
    uint32_t port;                   /* 端口编号（GPIOA/GPIOB...，实现层自定义映射） */
    uint32_t pin;                    /* 引脚编号（PIN_0..PIN_15） */
    iot_gpio_mode_t mode;
} iot_gpio_config_t;

typedef struct {
    int (*init)(void *ctx, const iot_gpio_config_t *config);
    int (*write)(void *ctx, iot_gpio_level_t level);
    int (*read)(void *ctx, iot_gpio_level_t *level);
    int (*toggle)(void *ctx);
    void *ctx;
} iot_port_gpio_t;

/* ================= SPI ================= */
/* 参考 STM32 SPI_InitTypeDef：编号 + 速率 + CPOL/CPHA 模式 */
typedef enum {
    IOT_SPI_MODE_0 = 0,   /* CPOL=0, CPHA=0 */
    IOT_SPI_MODE_1 = 1,   /* CPOL=0, CPHA=1 */
    IOT_SPI_MODE_2 = 2,   /* CPOL=1, CPHA=0 */
    IOT_SPI_MODE_3 = 3    /* CPOL=1, CPHA=1 */
} iot_spi_mode_t;

typedef struct {
    uint32_t instance;               /* 编号：SPI1 / SPI2 / ... */
    uint32_t speed_hz;               /* 时钟速率 */
    iot_spi_mode_t mode;             /* CPOL/CPHA */
} iot_spi_config_t;

typedef struct {
    int (*init)(void *ctx, const iot_spi_config_t *config);
    int (*deinit)(void *ctx);
    int (*transfer)(void *ctx, const uint8_t *tx, uint8_t *rx, uint32_t len);
    void *ctx;
} iot_port_spi_t;

/* ================= I2C ================= */
/* 参考 STM32 I2C_InitTypeDef：编号 + 时钟速率 + 本机地址 */
typedef struct {
    uint32_t instance;               /* 编号：I2C1 / I2C2 / ... */
    uint32_t speed_hz;               /* 100000 标准 / 400000 快速 */
    uint16_t own_address;            /* 本机从机地址（主机模式可忽略） */
} iot_i2c_config_t;

typedef struct {
    int (*init)(void *ctx, const iot_i2c_config_t *config);
    int (*deinit)(void *ctx);
    int (*write)(void *ctx, uint16_t addr, const uint8_t *data, uint32_t len);
    int (*read)(void *ctx, uint16_t addr, uint8_t *data, uint32_t len);
    void *ctx;
} iot_port_i2c_t;

/* ================= ADC ================= */
/* 编号 + 通道 + 分辨率 */
typedef struct {
    uint32_t instance;               /* 编号：ADC1 / ADC2 / ... */
    uint32_t channel;                /* 通道编号 */
    uint8_t  resolution;             /* 分辨率 bit：8 / 10 / 12 */
} iot_adc_config_t;

typedef struct {
    int (*init)(void *ctx, const iot_adc_config_t *config);
    int (*read)(void *ctx, uint16_t *value);   /* 读一次采样原始值 */
    int (*start)(void *ctx);
    int (*stop)(void *ctx);
    void *ctx;
} iot_port_adc_t;

/* ================= PWM ================= */
/* 编号（定时器）+ 通道 + 频率 */
typedef struct {
    uint32_t instance;               /* 编号：TIM1 / TIM2 / ... */
    uint32_t channel;                /* 通道编号：CH1..CH4 */
    uint32_t frequency;              /* 频率 Hz */
} iot_pwm_config_t;

typedef struct {
    int (*init)(void *ctx, const iot_pwm_config_t *config);
    int (*set_frequency)(void *ctx, uint32_t frequency);
    int (*set_duty)(void *ctx, uint16_t duty);   /* 占空比，0..1000 = 0..100% */
    int (*enable)(void *ctx);
    int (*disable)(void *ctx);
    void *ctx;
} iot_port_pwm_t;

/* ================= Flash ================= */
typedef struct {
    int (*init)(void *ctx);
    int (*read)(void *ctx, uint32_t addr, void *data, uint32_t len);
    int (*write)(void *ctx, uint32_t addr, const void *data, uint32_t len);
    int (*erase)(void *ctx, uint32_t addr, uint32_t len);
    uint32_t (*get_size)(void *ctx);
    uint32_t (*get_erase_size)(void *ctx);
    void *ctx;
} iot_port_flash_t;

/* ================= CAN ================= */
typedef void (*iot_port_can_rx_fn)(uint32_t id, const uint8_t *data,
                                   uint8_t len, void *user);

typedef struct {
    int (*init)(void *ctx, uint32_t baud);
    int (*send)(void *ctx, uint32_t id, const uint8_t *data, uint8_t len);
    int (*set_rx)(void *ctx, iot_port_can_rx_fn cb, void *user);
    void *ctx;
} iot_port_can_t;

/* ================= Watchdog ================= */
typedef struct {
    int (*init)(void *ctx, uint32_t timeout_ms);
    int (*feed)(void *ctx);
    void *ctx;
} iot_port_watchdog_t;

/* ================= 统一 Port 集合 ================= */
typedef struct {
    iot_port_system_t system;
    /* 其他外设不在此展开，统一通过 iot_port_register_* 注册、iot_port_get_* 获取 */
} iot_port_t;

/* ---------- 系统入口 ---------- */
int iot_port_init(const iot_port_t *port);
const iot_port_t *iot_port_get(void);

/* ---------- 外设实例注册 / 获取（按名，name 为 NULL 时 get 返回首个已注册实例） ---------- */
int iot_port_register_uart(const char *name, const iot_port_uart_t *uart);
const iot_port_uart_t *iot_port_get_uart(const char *name);

int iot_port_register_gpio(const char *name, const iot_port_gpio_t *gpio);
const iot_port_gpio_t *iot_port_get_gpio(const char *name);

int iot_port_register_spi(const char *name, const iot_port_spi_t *spi);
const iot_port_spi_t *iot_port_get_spi(const char *name);

int iot_port_register_i2c(const char *name, const iot_port_i2c_t *i2c);
const iot_port_i2c_t *iot_port_get_i2c(const char *name);

int iot_port_register_adc(const char *name, const iot_port_adc_t *adc);
const iot_port_adc_t *iot_port_get_adc(const char *name);

int iot_port_register_pwm(const char *name, const iot_port_pwm_t *pwm);
const iot_port_pwm_t *iot_port_get_pwm(const char *name);

int iot_port_register_flash(const char *name, const iot_port_flash_t *flash);
const iot_port_flash_t *iot_port_get_flash(const char *name);

int iot_port_register_can(const char *name, const iot_port_can_t *can);
const iot_port_can_t *iot_port_get_can(const char *name);

int iot_port_register_watchdog(const char *name, const iot_port_watchdog_t *wdg);
const iot_port_watchdog_t *iot_port_get_watchdog(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* IOT_PORT_H */