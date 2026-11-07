/*
 * 主机模拟平台（Host Simulator Platform）
 *
 * 该文件在 PC 上以纯软件方式实现 PORT 公共 API，不做运行时 ops 分发。
 * 所有外设状态保存在静态内存中，便于单元测试通过 port_sim.h 注入数据。
 * 不进行任何动态内存分配，也不依赖 OS。
 */

#include "port_sim.h"

#include <string.h>

/* ============================== 常量与私有状态 ============================== */

#define SIM_GPIO_PORTS      16U
#define SIM_GPIO_PINS       32U

#define SIM_UART_MAX        8
#define SIM_UART_BUF_SIZE   4096U

#define SIM_CAN_MAX         4
#define SIM_CAN_QUEUE       16
#define SIM_CAN_FILTERS     16

#define SIM_I2S_BUF_SIZE    1024U

#define SIM_ADC_CHANNELS    32U
#define SIM_DAC_CHANNELS    8U

#define SIM_MUTEX_MAX       8
#define SIM_SEM_MAX         8

#define SIM_I2S_MAX         4
#define SIM_TIM_MAX         4
#define SIM_CAPTURE_MAX     4
#define SIM_ENCODER_MAX     4
#define SIM_COMP_MAX        4
#define SIM_CRC_MAX         4

#define SIM_FLASH_SIZE      (64U * 1024U)
#define SIM_FLASH_ERASE     1024U

#define SIM_SDIO_BLOCK_SIZE 512U
#define SIM_SDIO_BLOCKS     (4U * 1024U * 1024U / SIM_SDIO_BLOCK_SIZE)

#define SIM_ETH_FRAME_MAX   2048U

/* ------------------------------ 核心 ------------------------------ */

static uint32_t s_tick;
static bool s_irq_enabled = true;
static bool s_reset_called;
static bool s_sleep_called;
static bool s_deepsleep_called;
static bool s_platform_initialized;

/* ------------------------------ GPIO / EXTI ------------------------------ */

typedef struct {
    bool initialized;
    port_gpio_mode_t mode;
    port_gpio_pull_t pull;
    port_gpio_level_t level;
} sim_gpio_t;

static sim_gpio_t s_gpio[SIM_GPIO_PORTS][SIM_GPIO_PINS];

typedef struct {
    bool initialized;
    bool enabled;
    port_exti_config_t config;
} sim_exti_t;

static sim_exti_t s_exti[SIM_GPIO_PORTS][SIM_GPIO_PINS];

/* ------------------------------ UART ------------------------------ */

typedef struct {
    bool used;
    const void *owner;
    uint8_t tx[SIM_UART_BUF_SIZE];
    size_t tx_len;
    uint8_t rx[SIM_UART_BUF_SIZE];
    size_t rx_len;
} sim_uart_ctx_t;

static sim_uart_ctx_t s_uart[SIM_UART_MAX];

/* ------------------------------ I2C ------------------------------ */

static uint8_t s_i2c_mem[256][256];
static uint8_t s_i2c_reg;

/* ------------------------------ I2S ------------------------------ */

static uint8_t s_i2s_buf[SIM_I2S_BUF_SIZE];
static size_t s_i2s_len;

/* ------------------------------ CAN ------------------------------ */

typedef struct {
    bool used;
    const void *owner;
    port_can_frame_t queue[SIM_CAN_QUEUE];
    uint32_t head;
    uint32_t count;
    port_can_filter_t filters[SIM_CAN_FILTERS];
    uint32_t filter_count;
} sim_can_ctx_t;

static sim_can_ctx_t s_can[SIM_CAN_MAX];

/* ------------------------------ ADC / DAC ------------------------------ */

static uint32_t s_adc_value[SIM_ADC_CHANNELS];
static uint32_t s_adc_max[SIM_ADC_CHANNELS];
static uint32_t s_dac_value[SIM_DAC_CHANNELS];

/* ------------------------------ COMP ------------------------------ */

typedef struct {
    bool used;
    const void *owner;
    bool value;
} sim_comp_ctx_t;

static sim_comp_ctx_t s_comp[SIM_COMP_MAX];

/* ------------------------------ TIM / CAPTURE / ENCODER ------------------------------ */

typedef struct {
    bool used;
    const void *owner;
    uint32_t counter;
    bool started;
} sim_tim_ctx_t;

static sim_tim_ctx_t s_tim[SIM_TIM_MAX];

typedef struct {
    bool used;
    const void *owner;
    uint32_t value;
} sim_capture_ctx_t;

static sim_capture_ctx_t s_capture[SIM_CAPTURE_MAX];

typedef struct {
    bool used;
    const void *owner;
    int32_t count;
} sim_encoder_ctx_t;

static sim_encoder_ctx_t s_encoder[SIM_ENCODER_MAX];

/* ------------------------------ WDG ------------------------------ */

static uint32_t s_wdg_refresh_count;
static bool s_wdg_reset_flag;

/* ------------------------------ RTC ------------------------------ */

static port_rtc_time_t s_rtc_time;

typedef struct {
    bool enabled;
    bool fired;
    port_rtc_time_t time;
    port_rtc_alarm_callback_t callback;
    void *user_data;
} sim_rtc_alarm_t;

static sim_rtc_alarm_t s_rtc_alarm[2];

/* ------------------------------ FLASH ------------------------------ */

static uint8_t s_flash[SIM_FLASH_SIZE];
static bool s_flash_ready;

/* ------------------------------ CRC ------------------------------ */

typedef struct {
    bool used;
    const void *owner;
    uint32_t state;
} sim_crc_ctx_t;

static sim_crc_ctx_t s_crc[SIM_CRC_MAX];

/* ------------------------------ RNG ------------------------------ */

#define SIM_RNG_SEED 0x12345678U
static uint32_t s_rng_state = SIM_RNG_SEED;

/* ------------------------------ PWR ------------------------------ */

static port_pwr_status_t s_pwr_status;

/* ------------------------------ QSPI ------------------------------ */

static bool s_qspi_mapped;

/* ------------------------------ SDIO ------------------------------ */

static uint8_t s_sdio[SIM_SDIO_BLOCK_SIZE * SIM_SDIO_BLOCKS];

/* ------------------------------ USB ------------------------------ */

static bool s_usb_started;
static bool s_usb_connected;

/* ------------------------------ ETH ------------------------------ */

static bool s_eth_started;
static uint8_t s_eth_frame[SIM_ETH_FRAME_MAX];
static size_t s_eth_len;

/* ============================== 通用辅助 ============================== */

static int sim_priv_index(const void *priv)
{
    if (priv == NULL) {
        return -1;
    }
    return (int)((uintptr_t)priv) - 1;
}

static bool sim_gpio_valid(port_pin_t pin)
{
    return PORT_PIN_GET_PORT(pin) < SIM_GPIO_PORTS &&
           PORT_PIN_GET_INDEX(pin) < SIM_GPIO_PINS;
}

static void sim_handle_ready(port_handle_base_t *base, port_state_t state)
{
    base->state = state;
    base->error = PORT_OK;
    base->initialized = true;
}

static void sim_handle_deinit(port_handle_base_t *base)
{
    base->state = PORT_STATE_RESET;
    base->error = PORT_OK;
    base->initialized = false;
}

/* ============================== 平台注册 ============================== */

static port_status_t sim_platform_init(void)
{
    s_platform_initialized = true;
    return PORT_OK;
}

static port_status_t sim_platform_deinit(void)
{
    s_platform_initialized = false;
    return PORT_OK;
}

static port_status_t sim_platform_get_info(port_mcu_info_t *info)
{
    if (info == NULL) {
        return PORT_INVALID_PARAM;
    }
    memset(info, 0, sizeof(*info));
    info->family = PORT_MCU_SIMULATOR;
    info->model = 1;
    info->revision = 1;
    info->core_clock = 72000000U;
    info->ahb_clock = 72000000U;
    info->apb1_clock = 36000000U;
    info->apb2_clock = 72000000U;
    info->flash_size = 1024U * 1024U;
    info->sram_size = 512U * 1024U;
    return PORT_OK;
}

port_status_t port_platform_install(void)
{
    static const port_platform_ops_t platform_ops = {
        "simulator",
        PORT_MCU_SIMULATOR,
        sim_platform_init,
        sim_platform_deinit,
        sim_platform_get_info
    };
    return port_platform_register(&platform_ops);
}

/* ============================== 核心 ============================== */

uint32_t port_tick_get(void)
{
    return s_tick;
}

void port_delay_ms(uint32_t ms)
{
    s_tick += ms;
}

void port_delay_us(uint32_t us)
{
    if (us > 0) {
        s_tick += (us + 999U) / 1000U;
    }
}

void port_irq_enable(void)
{
    s_irq_enabled = true;
}

void port_irq_disable(void)
{
    s_irq_enabled = false;
}

void port_system_reset(void)
{
    s_reset_called = true;
}

void port_system_sleep(void)
{
    s_sleep_called = true;
}

void port_system_deepsleep(void)
{
    s_deepsleep_called = true;
}

uint32_t port_get_capabilities(port_device_type_t type, uint8_t instance)
{
    uint32_t caps;

    switch (type) {
    case PORT_DEVICE_GPIO:
        return (instance < SIM_GPIO_PORTS) ? 0U : 0U;
    case PORT_DEVICE_UART:
        caps = PORT_CAP_DMA | PORT_CAP_IT | PORT_CAP_IDLE_DETECT;
        break;
    case PORT_DEVICE_SPI:
        caps = PORT_CAP_DMA | PORT_CAP_IT;
        break;
    case PORT_DEVICE_I2C:
        caps = PORT_CAP_DMA | PORT_CAP_IT;
        break;
    case PORT_DEVICE_CAN:
        caps = PORT_CAP_IT;
        break;
    case PORT_DEVICE_ADC:
        caps = PORT_CAP_DMA | PORT_CAP_IT;
        break;
    case PORT_DEVICE_DAC:
        caps = PORT_CAP_DMA;
        break;
    case PORT_DEVICE_PWM:
        caps = 0U;
        break;
    case PORT_DEVICE_TIMER:
        caps = PORT_CAP_IT;
        break;
    case PORT_DEVICE_FLASH:
        caps = 0U;
        break;
    case PORT_DEVICE_USB:
        caps = 0U;
        break;
    case PORT_DEVICE_ETH:
        caps = PORT_CAP_DMA | PORT_CAP_IT;
        break;
    default:
        return 0U;
    }
    if (instance >= 8U) {
        return 0U;
    }
    return caps;
}

/* ============================== CLOCK / PIN ============================== */

static uint32_t s_clock_freq[6];

port_status_t port_clock_init(const port_clock_config_t *config)
{
    if (config == NULL) {
        return PORT_INVALID_PARAM;
    }
    s_clock_freq[PORT_CLOCK_CORE] = (config->pll_frequency != 0U) ?
                                    config->pll_frequency : config->frequency;
    s_clock_freq[PORT_CLOCK_AHB] = s_clock_freq[PORT_CLOCK_CORE];
    s_clock_freq[PORT_CLOCK_APB1] = s_clock_freq[PORT_CLOCK_CORE] / 2U;
    s_clock_freq[PORT_CLOCK_APB2] = s_clock_freq[PORT_CLOCK_CORE];
    s_clock_freq[PORT_CLOCK_APB3] = s_clock_freq[PORT_CLOCK_CORE];
    s_clock_freq[PORT_CLOCK_APB4] = s_clock_freq[PORT_CLOCK_CORE];
    return PORT_OK;
}

uint32_t port_clock_get_frequency(port_clock_bus_t bus)
{
    if ((uint32_t)bus >= PORT_ARRAY_SIZE(s_clock_freq)) {
        return 0U;
    }
    return s_clock_freq[bus];
}

port_status_t port_clock_enable(void *peripheral)
{
    if (peripheral == NULL) {
        return PORT_INVALID_PARAM;
    }
    return PORT_OK;
}

port_status_t port_clock_disable(void *peripheral)
{
    if (peripheral == NULL) {
        return PORT_INVALID_PARAM;
    }
    return PORT_OK;
}

port_status_t port_pin_init(const port_pin_config_t *config)
{
    if (config == NULL || !sim_gpio_valid(config->pin)) {
        return PORT_INVALID_PARAM;
    }
    return PORT_OK;
}

port_status_t port_pin_deinit(port_pin_t pin)
{
    if (!sim_gpio_valid(pin)) {
        return PORT_INVALID_PARAM;
    }
    return PORT_OK;
}

/* ============================== GPIO ============================== */

port_status_t port_gpio_init(port_pin_t pin, const port_gpio_config_t *config)
{
    sim_gpio_t *gpio;

    if (config == NULL || !sim_gpio_valid(pin)) {
        return PORT_INVALID_PARAM;
    }
    gpio = &s_gpio[PORT_PIN_GET_PORT(pin)][PORT_PIN_GET_INDEX(pin)];
    gpio->initialized = true;
    gpio->mode = config->mode;
    gpio->pull = config->pull;
    gpio->level = (config->mode == PORT_GPIO_MODE_OUTPUT) ?
                  config->initial_level : PORT_GPIO_LOW;
    return PORT_OK;
}

port_status_t port_gpio_deinit(port_pin_t pin)
{
    sim_gpio_t *gpio;

    if (!sim_gpio_valid(pin)) {
        return PORT_INVALID_PARAM;
    }
    gpio = &s_gpio[PORT_PIN_GET_PORT(pin)][PORT_PIN_GET_INDEX(pin)];
    gpio->initialized = false;
    gpio->mode = PORT_GPIO_MODE_INPUT;
    gpio->pull = PORT_GPIO_NOPULL;
    gpio->level = PORT_GPIO_LOW;
    return PORT_OK;
}

port_status_t port_gpio_write(port_pin_t pin, port_gpio_level_t level)
{
    if (!sim_gpio_valid(pin)) {
        return PORT_INVALID_PARAM;
    }
    if (level != PORT_GPIO_LOW && level != PORT_GPIO_HIGH) {
        return PORT_INVALID_PARAM;
    }
    s_gpio[PORT_PIN_GET_PORT(pin)][PORT_PIN_GET_INDEX(pin)].level = level;
    return PORT_OK;
}

port_status_t port_gpio_read(port_pin_t pin, port_gpio_level_t *level)
{
    if (level == NULL || !sim_gpio_valid(pin)) {
        return PORT_INVALID_PARAM;
    }
    *level = s_gpio[PORT_PIN_GET_PORT(pin)][PORT_PIN_GET_INDEX(pin)].level;
    return PORT_OK;
}

port_status_t port_gpio_toggle(port_pin_t pin)
{
    sim_gpio_t *gpio;

    if (!sim_gpio_valid(pin)) {
        return PORT_INVALID_PARAM;
    }
    gpio = &s_gpio[PORT_PIN_GET_PORT(pin)][PORT_PIN_GET_INDEX(pin)];
    gpio->level = (gpio->level == PORT_GPIO_HIGH) ? PORT_GPIO_LOW : PORT_GPIO_HIGH;
    return PORT_OK;
}

port_status_t port_gpio_set(port_pin_t pin)
{
    return port_gpio_write(pin, PORT_GPIO_HIGH);
}

port_status_t port_gpio_reset(port_pin_t pin)
{
    return port_gpio_write(pin, PORT_GPIO_LOW);
}

/* ============================== EXTI ============================== */

port_status_t port_exti_init(port_pin_t pin, const port_exti_config_t *config)
{
    sim_exti_t *exti;

    if (config == NULL || !sim_gpio_valid(pin)) {
        return PORT_INVALID_PARAM;
    }
    exti = &s_exti[PORT_PIN_GET_PORT(pin)][PORT_PIN_GET_INDEX(pin)];
    exti->config = *config;
    exti->initialized = true;
    exti->enabled = true;
    return PORT_OK;
}

port_status_t port_exti_enable(port_pin_t pin)
{
    sim_exti_t *exti;

    if (!sim_gpio_valid(pin)) {
        return PORT_INVALID_PARAM;
    }
    exti = &s_exti[PORT_PIN_GET_PORT(pin)][PORT_PIN_GET_INDEX(pin)];
    if (!exti->initialized) {
        return PORT_NOT_READY;
    }
    exti->enabled = true;
    return PORT_OK;
}

port_status_t port_exti_disable(port_pin_t pin)
{
    sim_exti_t *exti;

    if (!sim_gpio_valid(pin)) {
        return PORT_INVALID_PARAM;
    }
    exti = &s_exti[PORT_PIN_GET_PORT(pin)][PORT_PIN_GET_INDEX(pin)];
    exti->enabled = false;
    return PORT_OK;
}

port_status_t port_exti_clear(port_pin_t pin)
{
    if (!sim_gpio_valid(pin)) {
        return PORT_INVALID_PARAM;
    }
    return PORT_OK;
}

port_status_t port_exti_deinit(port_pin_t pin)
{
    sim_exti_t *exti;

    if (!sim_gpio_valid(pin)) {
        return PORT_INVALID_PARAM;
    }
    exti = &s_exti[PORT_PIN_GET_PORT(pin)][PORT_PIN_GET_INDEX(pin)];
    memset(exti, 0, sizeof(*exti));
    return PORT_OK;
}

/* ============================== DMA ============================== */

port_status_t port_dma_init(port_dma_handle_t *handle, const port_dma_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    sim_handle_ready(&handle->base, PORT_STATE_READY);
    return PORT_OK;
}

port_status_t port_dma_deinit(port_dma_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    sim_handle_deinit(&handle->base);
    return PORT_OK;
}

port_status_t port_dma_start(port_dma_handle_t *handle, uintptr_t src, uintptr_t dst, size_t length)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    if (handle->config.direction != PORT_DMA_MEMORY_TO_MEMORY) {
        return PORT_NOT_SUPPORTED;
    }
    if (length > 0U) {
        if (src == 0U || dst == 0U) {
            return PORT_INVALID_PARAM;
        }
        memcpy((void *)dst, (const void *)src, length);
    }
    handle->base.state = PORT_STATE_READY;
    if (handle->callback != NULL) {
        handle->callback(handle, PORT_EVENT_COMPLETE, handle->user_data);
    }
    return PORT_OK;
}

port_status_t port_dma_stop(port_dma_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

port_status_t port_dma_suspend(port_dma_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->base.state = PORT_STATE_SUSPENDED;
    return PORT_OK;
}

port_status_t port_dma_resume(port_dma_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

size_t port_dma_get_remaining(port_dma_handle_t *handle)
{
    PORT_UNUSED(handle);
    return 0U;
}

/* ============================== UART ============================== */

static sim_uart_ctx_t *sim_uart_ctx(port_uart_handle_t *handle, bool create)
{
    int index;

    if (handle == NULL) {
        return NULL;
    }
    index = sim_priv_index(handle->base.priv);
    if (index >= 0 && index < SIM_UART_MAX && s_uart[index].used &&
        s_uart[index].owner == handle) {
        return &s_uart[index];
    }
    if (!create) {
        return NULL;
    }
    for (index = 0; index < SIM_UART_MAX; index++) {
        if (!s_uart[index].used) {
            s_uart[index].used = true;
            s_uart[index].owner = handle;
            s_uart[index].tx_len = 0U;
            s_uart[index].rx_len = 0U;
            handle->base.priv = (void *)(uintptr_t)(index + 1);
            return &s_uart[index];
        }
    }
    return NULL;
}

static void sim_uart_emit_tx(port_uart_handle_t *handle, size_t length)
{
    if (handle->callback != NULL) {
        handle->callback(handle, PORT_UART_EVENT_TX_COMPLETE, length, handle->user_data);
    }
}

static size_t sim_uart_append_tx(sim_uart_ctx_t *ctx, const uint8_t *data, size_t length)
{
    size_t space = SIM_UART_BUF_SIZE - ctx->tx_len;
    size_t n = PORT_MIN(length, space);

    if (n > 0U) {
        memcpy(&ctx->tx[ctx->tx_len], data, n);
        ctx->tx_len += n;
    }
    return n;
}

static size_t sim_uart_consume_rx(sim_uart_ctx_t *ctx, uint8_t *data, size_t length)
{
    size_t n = PORT_MIN(length, ctx->rx_len);

    if (n > 0U) {
        memcpy(data, ctx->rx, n);
        if (n < ctx->rx_len) {
            memmove(ctx->rx, &ctx->rx[n], ctx->rx_len - n);
        }
        ctx->rx_len -= n;
    }
    return n;
}

port_status_t port_uart_init(port_uart_handle_t *handle, const port_uart_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (sim_uart_ctx(handle, true) == NULL) {
        return PORT_NO_MEMORY;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    sim_handle_ready(&handle->base, PORT_STATE_READY);
    return PORT_OK;
}

port_status_t port_uart_deinit(port_uart_handle_t *handle)
{
    int index;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    index = sim_priv_index(handle->base.priv);
    if (index >= 0 && index < SIM_UART_MAX && s_uart[index].owner == handle) {
        s_uart[index].used = false;
        s_uart[index].owner = NULL;
        s_uart[index].tx_len = 0U;
        s_uart[index].rx_len = 0U;
    }
    handle->base.priv = NULL;
    sim_handle_deinit(&handle->base);
    return PORT_OK;
}

port_status_t port_uart_transmit(port_uart_handle_t *handle, const uint8_t *data,
                                 size_t length, uint32_t timeout)
{
    sim_uart_ctx_t *ctx;

    if (handle == NULL || (data == NULL && length > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    ctx = sim_uart_ctx(handle, false);
    if (ctx == NULL) {
        return PORT_NOT_READY;
    }
    PORT_UNUSED(timeout);
    sim_uart_append_tx(ctx, data, length);
    sim_uart_emit_tx(handle, length);
    return PORT_OK;
}

port_status_t port_uart_receive(port_uart_handle_t *handle, uint8_t *data,
                                size_t length, uint32_t timeout)
{
    sim_uart_ctx_t *ctx;
    size_t n;

    if (handle == NULL || (data == NULL && length > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    if (length == 0U) {
        return PORT_OK;
    }
    ctx = sim_uart_ctx(handle, false);
    if (ctx == NULL || ctx->rx_len == 0U) {
        return PORT_TIMEOUT;
    }
    n = sim_uart_consume_rx(ctx, data, length);
    if (n == 0U) {
        return PORT_TIMEOUT;
    }
    if (n < length && timeout == 0U) {
        return PORT_TIMEOUT;
    }
    return PORT_OK;
}

port_status_t port_uart_transmit_it(port_uart_handle_t *handle, const uint8_t *data, size_t length)
{
    sim_uart_ctx_t *ctx;

    if (handle == NULL || (data == NULL && length > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    ctx = sim_uart_ctx(handle, false);
    if (ctx == NULL) {
        return PORT_NOT_READY;
    }
    sim_uart_append_tx(ctx, data, length);
    sim_uart_emit_tx(handle, length);
    return PORT_OK;
}

port_status_t port_uart_receive_it(port_uart_handle_t *handle, uint8_t *data, size_t length)
{
    sim_uart_ctx_t *ctx;
    size_t n;

    if (handle == NULL || (data == NULL && length > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    if (length == 0U) {
        return PORT_OK;
    }
    ctx = sim_uart_ctx(handle, false);
    if (ctx == NULL || ctx->rx_len == 0U) {
        return PORT_TIMEOUT;
    }
    n = sim_uart_consume_rx(ctx, data, length);
    if (handle->callback != NULL) {
        handle->callback(handle, PORT_UART_EVENT_RX_COMPLETE, n, handle->user_data);
    }
    return PORT_OK;
}

port_status_t port_uart_transmit_dma(port_uart_handle_t *handle, const uint8_t *data, size_t length)
{
    return port_uart_transmit_it(handle, data, length);
}

port_status_t port_uart_receive_dma(port_uart_handle_t *handle, uint8_t *data, size_t length)
{
    return port_uart_receive_it(handle, data, length);
}

port_status_t port_uart_receive_to_idle(port_uart_handle_t *handle, uint8_t *data,
                                        size_t length, port_transfer_mode_t mode)
{
    sim_uart_ctx_t *ctx;
    size_t n;

    if (handle == NULL || (data == NULL && length > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    PORT_UNUSED(mode);
    if (length == 0U) {
        return PORT_OK;
    }
    ctx = sim_uart_ctx(handle, false);
    if (ctx == NULL || ctx->rx_len == 0U) {
        return PORT_TIMEOUT;
    }
    n = sim_uart_consume_rx(ctx, data, length);
    if (handle->callback != NULL) {
        handle->callback(handle, PORT_UART_EVENT_RX_IDLE, n, handle->user_data);
    }
    return PORT_OK;
}

size_t port_uart_get_rx_count(port_uart_handle_t *handle)
{
    sim_uart_ctx_t *ctx;

    if (handle == NULL) {
        return 0U;
    }
    ctx = sim_uart_ctx(handle, false);
    return (ctx != NULL) ? ctx->rx_len : 0U;
}

port_status_t port_uart_abort_tx(port_uart_handle_t *handle)
{
    sim_uart_ctx_t *ctx;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    ctx = sim_uart_ctx(handle, false);
    if (ctx != NULL) {
        ctx->tx_len = 0U;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

port_status_t port_uart_abort_rx(port_uart_handle_t *handle)
{
    sim_uart_ctx_t *ctx;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    ctx = sim_uart_ctx(handle, false);
    if (ctx != NULL) {
        ctx->rx_len = 0U;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

port_status_t port_uart_abort(port_uart_handle_t *handle)
{
    sim_uart_ctx_t *ctx;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    ctx = sim_uart_ctx(handle, false);
    if (ctx != NULL) {
        ctx->tx_len = 0U;
        ctx->rx_len = 0U;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

port_state_t port_uart_get_state(port_uart_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_STATE_RESET;
    }
    return handle->base.state;
}

port_status_t port_uart_get_error(port_uart_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    return handle->base.error;
}

port_status_t port_uart_register_callback(port_uart_handle_t *handle,
                                          port_uart_callback_t callback, void *user_data)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->callback = callback;
    handle->user_data = user_data;
    return PORT_OK;
}

/* ============================== SPI ============================== */

port_status_t port_spi_init(port_spi_handle_t *handle, const port_spi_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    sim_handle_ready(&handle->base, PORT_STATE_READY);
    return PORT_OK;
}

port_status_t port_spi_deinit(port_spi_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    sim_handle_deinit(&handle->base);
    return PORT_OK;
}

port_status_t port_spi_transmit(port_spi_handle_t *handle, const void *data,
                                size_t length, uint32_t timeout)
{
    if (handle == NULL || (data == NULL && length > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    PORT_UNUSED(timeout);
    return PORT_OK;
}

port_status_t port_spi_receive(port_spi_handle_t *handle, void *data,
                               size_t length, uint32_t timeout)
{
    if (handle == NULL || (data == NULL && length > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    PORT_UNUSED(timeout);
    if (length > 0U) {
        memset(data, 0, length);
    }
    return PORT_OK;
}

port_status_t port_spi_transfer(port_spi_handle_t *handle, const void *tx_data,
                                void *rx_data, size_t length, uint32_t timeout)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    if (length == 0U) {
        return PORT_OK;
    }
    if (tx_data == NULL || rx_data == NULL) {
        return PORT_INVALID_PARAM;
    }
    PORT_UNUSED(timeout);
    memcpy(rx_data, tx_data, length);
    return PORT_OK;
}

port_status_t port_spi_transmit_dma(port_spi_handle_t *handle, const void *data, size_t length)
{
    return port_spi_transmit(handle, data, length, PORT_TIMEOUT_FOREVER);
}

port_status_t port_spi_receive_dma(port_spi_handle_t *handle, void *data, size_t length)
{
    return port_spi_receive(handle, data, length, PORT_TIMEOUT_FOREVER);
}

port_status_t port_spi_transfer_dma(port_spi_handle_t *handle, const void *tx_data,
                                    void *rx_data, size_t length)
{
    return port_spi_transfer(handle, tx_data, rx_data, length, PORT_TIMEOUT_FOREVER);
}

port_status_t port_spi_set_speed(port_spi_handle_t *handle, uint32_t baudrate)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    handle->config.baudrate = baudrate;
    return PORT_OK;
}

port_status_t port_spi_abort(port_spi_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

port_state_t port_spi_get_state(port_spi_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_STATE_RESET;
    }
    return handle->base.state;
}

/* ============================== I2C ============================== */

static bool sim_i2c_addr_valid(uint16_t address)
{
    return address <= 0xFFU;
}

port_status_t port_i2c_init(port_i2c_handle_t *handle, const port_i2c_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    sim_handle_ready(&handle->base, PORT_STATE_READY);
    return PORT_OK;
}

port_status_t port_i2c_deinit(port_i2c_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    sim_handle_deinit(&handle->base);
    return PORT_OK;
}

port_status_t port_i2c_master_transmit(port_i2c_handle_t *handle, uint16_t address,
                                       const uint8_t *data, size_t length, uint32_t timeout)
{
    size_t i;

    if (handle == NULL || (data == NULL && length > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    if (!sim_i2c_addr_valid(address)) {
        return PORT_INVALID_PARAM;
    }
    PORT_UNUSED(timeout);
    if (length == 0U) {
        return PORT_OK;
    }
    s_i2c_reg = data[0];
    for (i = 1U; i < length; i++) {
        s_i2c_mem[address][s_i2c_reg & 0xFFU] = data[i];
        s_i2c_reg = (uint8_t)((s_i2c_reg + 1U) & 0xFFU);
    }
    return PORT_OK;
}

port_status_t port_i2c_master_receive(port_i2c_handle_t *handle, uint16_t address,
                                      uint8_t *data, size_t length, uint32_t timeout)
{
    size_t i;

    if (handle == NULL || (data == NULL && length > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    if (!sim_i2c_addr_valid(address)) {
        return PORT_INVALID_PARAM;
    }
    PORT_UNUSED(timeout);
    for (i = 0U; i < length; i++) {
        data[i] = s_i2c_mem[address][s_i2c_reg & 0xFFU];
        s_i2c_reg = (uint8_t)((s_i2c_reg + 1U) & 0xFFU);
    }
    return PORT_OK;
}

port_status_t port_i2c_master_transfer(port_i2c_handle_t *handle, uint16_t address,
                                       const uint8_t *tx_data, size_t tx_length,
                                       uint8_t *rx_data, size_t rx_length, uint32_t timeout)
{
    port_status_t status;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    if (tx_length > 0U && tx_data == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (rx_length > 0U && rx_data == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (tx_length > 0U) {
        status = port_i2c_master_transmit(handle, address, tx_data, tx_length, timeout);
        if (status != PORT_OK) {
            return status;
        }
    }
    if (rx_length > 0U) {
        return port_i2c_master_receive(handle, address, rx_data, rx_length, timeout);
    }
    return PORT_OK;
}

port_status_t port_i2c_mem_write(port_i2c_handle_t *handle, uint16_t address,
                                 uint16_t reg, uint8_t reg_size,
                                 const uint8_t *data, size_t length, uint32_t timeout)
{
    size_t i;

    if (handle == NULL || (data == NULL && length > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    if (reg_size != 1U && reg_size != 2U) {
        return PORT_INVALID_PARAM;
    }
    if (!sim_i2c_addr_valid(address)) {
        return PORT_INVALID_PARAM;
    }
    PORT_UNUSED(timeout);
    s_i2c_reg = (uint8_t)(reg & 0xFFU);
    for (i = 0U; i < length; i++) {
        s_i2c_mem[address][s_i2c_reg & 0xFFU] = data[i];
        s_i2c_reg = (uint8_t)((s_i2c_reg + 1U) & 0xFFU);
    }
    return PORT_OK;
}

port_status_t port_i2c_mem_read(port_i2c_handle_t *handle, uint16_t address,
                                uint16_t reg, uint8_t reg_size,
                                uint8_t *data, size_t length, uint32_t timeout)
{
    size_t i;

    if (handle == NULL || (data == NULL && length > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    if (reg_size != 1U && reg_size != 2U) {
        return PORT_INVALID_PARAM;
    }
    if (!sim_i2c_addr_valid(address)) {
        return PORT_INVALID_PARAM;
    }
    PORT_UNUSED(timeout);
    s_i2c_reg = (uint8_t)(reg & 0xFFU);
    for (i = 0U; i < length; i++) {
        data[i] = s_i2c_mem[address][s_i2c_reg & 0xFFU];
        s_i2c_reg = (uint8_t)((s_i2c_reg + 1U) & 0xFFU);
    }
    return PORT_OK;
}

port_status_t port_i2c_master_transmit_dma(port_i2c_handle_t *handle, uint16_t address,
                                           const uint8_t *data, size_t length)
{
    return port_i2c_master_transmit(handle, address, data, length, PORT_TIMEOUT_FOREVER);
}

port_status_t port_i2c_master_receive_dma(port_i2c_handle_t *handle, uint16_t address,
                                          uint8_t *data, size_t length)
{
    return port_i2c_master_receive(handle, address, data, length, PORT_TIMEOUT_FOREVER);
}

port_status_t port_i2c_is_device_ready(port_i2c_handle_t *handle, uint16_t address,
                                       uint32_t trials, uint32_t timeout)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    PORT_UNUSED(trials);
    PORT_UNUSED(timeout);
    return (address <= 0x7FU) ? PORT_OK : PORT_NOT_FOUND;
}

port_status_t port_i2c_bus_recover(port_i2c_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

port_status_t port_i2c_abort(port_i2c_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

port_state_t port_i2c_get_state(port_i2c_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_STATE_RESET;
    }
    return handle->base.state;
}

/* ============================== I2S ============================== */

port_status_t port_i2s_init(port_i2s_handle_t *handle, const port_i2s_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    sim_handle_ready(&handle->base, PORT_STATE_READY);
    return PORT_OK;
}

port_status_t port_i2s_deinit(port_i2s_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    sim_handle_deinit(&handle->base);
    return PORT_OK;
}

port_status_t port_i2s_transmit(port_i2s_handle_t *handle, const void *data,
                                size_t length, uint32_t timeout)
{
    size_t n;

    if (handle == NULL || (data == NULL && length > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    PORT_UNUSED(timeout);
    n = PORT_MIN(length, SIM_I2S_BUF_SIZE);
    if (n > 0U) {
        memcpy(s_i2s_buf, data, n);
    }
    s_i2s_len = n;
    return PORT_OK;
}

port_status_t port_i2s_receive(port_i2s_handle_t *handle, void *data,
                               size_t length, uint32_t timeout)
{
    size_t n;

    if (handle == NULL || (data == NULL && length > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    PORT_UNUSED(timeout);
    n = PORT_MIN(length, s_i2s_len);
    if (n > 0U) {
        memcpy(data, s_i2s_buf, n);
    }
    if (length > n) {
        memset((uint8_t *)data + n, 0, length - n);
    }
    return PORT_OK;
}

port_status_t port_i2s_transmit_dma(port_i2s_handle_t *handle, const void *data, size_t length)
{
    return port_i2s_transmit(handle, data, length, PORT_TIMEOUT_FOREVER);
}

port_status_t port_i2s_receive_dma(port_i2s_handle_t *handle, void *data, size_t length)
{
    return port_i2s_receive(handle, data, length, PORT_TIMEOUT_FOREVER);
}

port_status_t port_i2s_abort(port_i2s_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

/* ============================== CAN ============================== */

static sim_can_ctx_t *sim_can_ctx(port_can_handle_t *handle, bool create)
{
    int index;

    if (handle == NULL) {
        return NULL;
    }
    index = sim_priv_index(handle->base.priv);
    if (index >= 0 && index < SIM_CAN_MAX && s_can[index].used &&
        s_can[index].owner == handle) {
        return &s_can[index];
    }
    if (!create) {
        return NULL;
    }
    for (index = 0; index < SIM_CAN_MAX; index++) {
        if (!s_can[index].used) {
            memset(&s_can[index], 0, sizeof(s_can[index]));
            s_can[index].used = true;
            s_can[index].owner = handle;
            handle->base.priv = (void *)(uintptr_t)(index + 1);
            return &s_can[index];
        }
    }
    return NULL;
}

port_status_t port_can_init(port_can_handle_t *handle, const port_can_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (sim_can_ctx(handle, true) == NULL) {
        return PORT_NO_MEMORY;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    sim_handle_ready(&handle->base, PORT_STATE_READY);
    return PORT_OK;
}

port_status_t port_can_deinit(port_can_handle_t *handle)
{
    int index;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    index = sim_priv_index(handle->base.priv);
    if (index >= 0 && index < SIM_CAN_MAX && s_can[index].owner == handle) {
        memset(&s_can[index], 0, sizeof(s_can[index]));
    }
    handle->base.priv = NULL;
    sim_handle_deinit(&handle->base);
    return PORT_OK;
}

port_status_t port_can_start(port_can_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

port_status_t port_can_stop(port_can_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->base.state = PORT_STATE_RESET;
    return PORT_OK;
}

port_status_t port_can_transmit(port_can_handle_t *handle, const port_can_frame_t *frame,
                                uint32_t timeout)
{
    if (handle == NULL || frame == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    PORT_UNUSED(timeout);
    if ((handle->config.mode == PORT_CAN_MODE_LOOPBACK ||
         handle->config.mode == PORT_CAN_MODE_SILENT_LOOPBACK) &&
        handle->callback != NULL) {
        handle->callback(handle, PORT_EVENT_RX_COMPLETE, handle->user_data);
    }
    return PORT_OK;
}

port_status_t port_can_receive(port_can_handle_t *handle, port_can_frame_t *frame,
                               uint32_t timeout)
{
    sim_can_ctx_t *ctx;

    if (handle == NULL || frame == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    PORT_UNUSED(timeout);
    ctx = sim_can_ctx(handle, false);
    if (ctx == NULL || ctx->count == 0U) {
        return PORT_TIMEOUT;
    }
    *frame = ctx->queue[ctx->head];
    ctx->head = (ctx->head + 1U) % SIM_CAN_QUEUE;
    ctx->count--;
    return PORT_OK;
}

port_status_t port_can_transmit_it(port_can_handle_t *handle, const port_can_frame_t *frame)
{
    return port_can_transmit(handle, frame, PORT_TIMEOUT_FOREVER);
}

port_status_t port_can_add_filter(port_can_handle_t *handle, const port_can_filter_t *filter)
{
    sim_can_ctx_t *ctx;

    if (handle == NULL || filter == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    ctx = sim_can_ctx(handle, false);
    if (ctx == NULL) {
        return PORT_NOT_READY;
    }
    if (ctx->filter_count >= SIM_CAN_FILTERS) {
        return PORT_NO_MEMORY;
    }
    ctx->filters[ctx->filter_count] = *filter;
    ctx->filter_count++;
    return PORT_OK;
}

port_status_t port_can_clear_filters(port_can_handle_t *handle)
{
    sim_can_ctx_t *ctx;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    ctx = sim_can_ctx(handle, false);
    if (ctx != NULL) {
        ctx->filter_count = 0U;
    }
    return PORT_OK;
}

/* ============================== ADC ============================== */

port_status_t port_adc_init(port_adc_handle_t *handle, const port_adc_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    sim_handle_ready(&handle->base, PORT_STATE_READY);
    return PORT_OK;
}

port_status_t port_adc_deinit(port_adc_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    sim_handle_deinit(&handle->base);
    return PORT_OK;
}

port_status_t port_adc_config_channel(port_adc_handle_t *handle,
                                      const port_adc_channel_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    if (config->channel >= SIM_ADC_CHANNELS) {
        return PORT_INVALID_PARAM;
    }
    return PORT_OK;
}

port_status_t port_adc_start(port_adc_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

port_status_t port_adc_stop(port_adc_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

port_status_t port_adc_read(port_adc_handle_t *handle, uint8_t channel,
                            uint32_t *value, uint32_t timeout)
{
    uint32_t max_value;

    if (handle == NULL || value == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    if (channel >= SIM_ADC_CHANNELS) {
        return PORT_INVALID_PARAM;
    }
    PORT_UNUSED(timeout);
    max_value = (uint32_t)((1UL << (uint32_t)handle->config.resolution) - 1UL);
    if (s_adc_max[channel] != 0U && s_adc_max[channel] < max_value) {
        max_value = s_adc_max[channel];
    }
    *value = PORT_MIN(s_adc_value[channel], max_value);
    return PORT_OK;
}

port_status_t port_adc_start_it(port_adc_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    if (handle->callback != NULL) {
        handle->callback(handle, PORT_EVENT_COMPLETE, handle->user_data);
    }
    return PORT_OK;
}

port_status_t port_adc_start_dma(port_adc_handle_t *handle, uint32_t *buffer, size_t length)
{
    uint32_t max_value;
    size_t i;

    if (handle == NULL || (buffer == NULL && length > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    max_value = (uint32_t)((1UL << (uint32_t)handle->config.resolution) - 1UL);
    for (i = 0U; i < length; i++) {
        uint32_t channel = (uint32_t)(i % SIM_ADC_CHANNELS);
        uint32_t limit = max_value;
        if (s_adc_max[channel] != 0U && s_adc_max[channel] < limit) {
            limit = s_adc_max[channel];
        }
        buffer[i] = PORT_MIN(s_adc_value[channel], limit);
    }
    if (handle->callback != NULL) {
        handle->callback(handle, PORT_EVENT_COMPLETE, handle->user_data);
    }
    return PORT_OK;
}

port_status_t port_adc_calibrate(port_adc_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    return PORT_OK;
}

uint32_t port_adc_get_max_value(port_adc_handle_t *handle)
{
    uint32_t width;

    if (handle == NULL) {
        return 0U;
    }
    width = (uint32_t)handle->config.resolution;
    if (width == 0U || width > 32U) {
        return 0U;
    }
    return (uint32_t)((1UL << width) - 1UL);
}

/* ============================== DAC ============================== */

port_status_t port_dac_init(port_dac_handle_t *handle, const port_dac_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->config = *config;
    sim_handle_ready(&handle->base, PORT_STATE_READY);
    return PORT_OK;
}

port_status_t port_dac_deinit(port_dac_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    sim_handle_deinit(&handle->base);
    return PORT_OK;
}

port_status_t port_dac_start(port_dac_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

port_status_t port_dac_stop(port_dac_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

port_status_t port_dac_write(port_dac_handle_t *handle, uint32_t value)
{
    uint8_t channel;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    channel = handle->config.channel;
    if (channel >= SIM_DAC_CHANNELS) {
        channel = 0U;
    }
    s_dac_value[channel] = (value > 4095U) ? 4095U : value;
    return PORT_OK;
}

port_status_t port_dac_write_dma(port_dac_handle_t *handle, const uint32_t *buffer, size_t length)
{
    size_t i;

    if (handle == NULL || (buffer == NULL && length > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    for (i = 0U; i < length; i++) {
        port_status_t status = port_dac_write(handle, buffer[i]);
        if (status != PORT_OK) {
            return status;
        }
    }
    return PORT_OK;
}

/* ============================== COMP / OPAMP ============================== */

static sim_comp_ctx_t *sim_comp_ctx(port_comp_handle_t *handle, bool create)
{
    int index;

    if (handle == NULL) {
        return NULL;
    }
    index = sim_priv_index(handle->base.priv);
    if (index >= 0 && index < SIM_COMP_MAX && s_comp[index].used &&
        s_comp[index].owner == handle) {
        return &s_comp[index];
    }
    if (!create) {
        return NULL;
    }
    for (index = 0; index < SIM_COMP_MAX; index++) {
        if (!s_comp[index].used) {
            s_comp[index].used = true;
            s_comp[index].owner = handle;
            s_comp[index].value = false;
            handle->base.priv = (void *)(uintptr_t)(index + 1);
            return &s_comp[index];
        }
    }
    return NULL;
}

port_status_t port_comp_init(port_comp_handle_t *handle, const port_comp_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (sim_comp_ctx(handle, true) == NULL) {
        return PORT_NO_MEMORY;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    sim_handle_ready(&handle->base, PORT_STATE_READY);
    return PORT_OK;
}

port_status_t port_comp_start(port_comp_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

port_status_t port_comp_stop(port_comp_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

port_status_t port_comp_read(port_comp_handle_t *handle, bool *value)
{
    sim_comp_ctx_t *ctx;

    if (handle == NULL || value == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    ctx = sim_comp_ctx(handle, false);
    *value = (ctx != NULL) ? ctx->value : false;
    return PORT_OK;
}

port_status_t port_opamp_init(port_opamp_handle_t *handle, const port_opamp_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->config = *config;
    sim_handle_ready(&handle->base, PORT_STATE_READY);
    return PORT_OK;
}

port_status_t port_opamp_start(port_opamp_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

port_status_t port_opamp_stop(port_opamp_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

/* ============================== TIM ============================== */

static sim_tim_ctx_t *sim_tim_ctx(port_tim_handle_t *handle, bool create)
{
    int index;

    if (handle == NULL) {
        return NULL;
    }
    index = sim_priv_index(handle->base.priv);
    if (index >= 0 && index < SIM_TIM_MAX && s_tim[index].used &&
        s_tim[index].owner == handle) {
        return &s_tim[index];
    }
    if (!create) {
        return NULL;
    }
    for (index = 0; index < SIM_TIM_MAX; index++) {
        if (!s_tim[index].used) {
            s_tim[index].used = true;
            s_tim[index].owner = handle;
            s_tim[index].counter = 0U;
            s_tim[index].started = false;
            handle->base.priv = (void *)(uintptr_t)(index + 1);
            return &s_tim[index];
        }
    }
    return NULL;
}

port_status_t port_tim_init(port_tim_handle_t *handle, const port_tim_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (sim_tim_ctx(handle, true) == NULL) {
        return PORT_NO_MEMORY;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    sim_handle_ready(&handle->base, PORT_STATE_READY);
    return PORT_OK;
}

port_status_t port_tim_deinit(port_tim_handle_t *handle)
{
    int index;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    index = sim_priv_index(handle->base.priv);
    if (index >= 0 && index < SIM_TIM_MAX && s_tim[index].owner == handle) {
        memset(&s_tim[index], 0, sizeof(s_tim[index]));
    }
    handle->base.priv = NULL;
    sim_handle_deinit(&handle->base);
    return PORT_OK;
}

port_status_t port_tim_start(port_tim_handle_t *handle)
{
    sim_tim_ctx_t *ctx;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    ctx = sim_tim_ctx(handle, false);
    if (ctx == NULL) {
        return PORT_NOT_READY;
    }
    ctx->started = true;
    handle->base.state = PORT_STATE_BUSY;
    return PORT_OK;
}

port_status_t port_tim_stop(port_tim_handle_t *handle)
{
    sim_tim_ctx_t *ctx;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    ctx = sim_tim_ctx(handle, false);
    if (ctx != NULL) {
        ctx->started = false;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

port_status_t port_tim_start_it(port_tim_handle_t *handle)
{
    sim_tim_ctx_t *ctx;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    ctx = sim_tim_ctx(handle, false);
    if (ctx == NULL) {
        return PORT_NOT_READY;
    }
    ctx->started = true;
    handle->base.state = PORT_STATE_BUSY;
    if (handle->callback != NULL) {
        handle->callback(handle, PORT_EVENT_COMPLETE, handle->user_data);
    }
    return PORT_OK;
}

port_status_t port_tim_stop_it(port_tim_handle_t *handle)
{
    return port_tim_stop(handle);
}

port_status_t port_tim_set_period(port_tim_handle_t *handle, uint32_t period)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->config.period = period;
    return PORT_OK;
}

port_status_t port_tim_set_prescaler(port_tim_handle_t *handle, uint32_t prescaler)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->config.prescaler = prescaler;
    return PORT_OK;
}

port_status_t port_tim_set_counter(port_tim_handle_t *handle, uint32_t counter)
{
    sim_tim_ctx_t *ctx;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    ctx = sim_tim_ctx(handle, false);
    if (ctx == NULL) {
        return PORT_NOT_READY;
    }
    ctx->counter = counter;
    return PORT_OK;
}

uint32_t port_tim_get_counter(port_tim_handle_t *handle)
{
    sim_tim_ctx_t *ctx;

    if (handle == NULL) {
        return 0U;
    }
    ctx = sim_tim_ctx(handle, false);
    return (ctx != NULL) ? ctx->counter : 0U;
}

/* ============================== PWM ============================== */

port_status_t port_pwm_init(port_pwm_handle_t *handle, const port_pwm_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (config->duty > 10000U) {
        return PORT_INVALID_PARAM;
    }
    handle->config = *config;
    sim_handle_ready(&handle->base, PORT_STATE_READY);
    return PORT_OK;
}

port_status_t port_pwm_deinit(port_pwm_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    sim_handle_deinit(&handle->base);
    return PORT_OK;
}

port_status_t port_pwm_start(port_pwm_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

port_status_t port_pwm_stop(port_pwm_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

port_status_t port_pwm_set_frequency(port_pwm_handle_t *handle, uint32_t frequency)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (frequency == 0U) {
        return PORT_INVALID_PARAM;
    }
    handle->config.frequency = frequency;
    return PORT_OK;
}

port_status_t port_pwm_set_duty(port_pwm_handle_t *handle, uint32_t duty)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (duty > 10000U) {
        return PORT_INVALID_PARAM;
    }
    handle->config.duty = duty;
    return PORT_OK;
}

port_status_t port_pwm_set_polarity(port_pwm_handle_t *handle, port_pwm_polarity_t polarity)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->config.polarity = polarity;
    return PORT_OK;
}

/* ============================== CAPTURE ============================== */

static sim_capture_ctx_t *sim_capture_ctx(port_capture_handle_t *handle, bool create)
{
    int index;

    if (handle == NULL) {
        return NULL;
    }
    index = sim_priv_index(handle->base.priv);
    if (index >= 0 && index < SIM_CAPTURE_MAX && s_capture[index].used &&
        s_capture[index].owner == handle) {
        return &s_capture[index];
    }
    if (!create) {
        return NULL;
    }
    for (index = 0; index < SIM_CAPTURE_MAX; index++) {
        if (!s_capture[index].used) {
            s_capture[index].used = true;
            s_capture[index].owner = handle;
            s_capture[index].value = 0U;
            handle->base.priv = (void *)(uintptr_t)(index + 1);
            return &s_capture[index];
        }
    }
    return NULL;
}

port_status_t port_capture_init(port_capture_handle_t *handle,
                                const port_capture_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (sim_capture_ctx(handle, true) == NULL) {
        return PORT_NO_MEMORY;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    sim_handle_ready(&handle->base, PORT_STATE_READY);
    return PORT_OK;
}

port_status_t port_capture_start(port_capture_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    handle->base.state = PORT_STATE_BUSY;
    return PORT_OK;
}

port_status_t port_capture_stop(port_capture_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

port_status_t port_capture_start_it(port_capture_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    if (sim_capture_ctx(handle, false) == NULL) {
        return PORT_NOT_READY;
    }
    handle->base.state = PORT_STATE_BUSY;
    if (handle->callback != NULL) {
        handle->callback(handle, PORT_EVENT_COMPLETE, handle->user_data);
    }
    return PORT_OK;
}

uint32_t port_capture_get_value(port_capture_handle_t *handle)
{
    sim_capture_ctx_t *ctx;

    if (handle == NULL) {
        return 0U;
    }
    ctx = sim_capture_ctx(handle, false);
    return (ctx != NULL) ? ctx->value : 0U;
}

/* ============================== ENCODER ============================== */

static sim_encoder_ctx_t *sim_encoder_ctx(port_encoder_handle_t *handle, bool create)
{
    int index;

    if (handle == NULL) {
        return NULL;
    }
    index = sim_priv_index(handle->base.priv);
    if (index >= 0 && index < SIM_ENCODER_MAX && s_encoder[index].used &&
        s_encoder[index].owner == handle) {
        return &s_encoder[index];
    }
    if (!create) {
        return NULL;
    }
    for (index = 0; index < SIM_ENCODER_MAX; index++) {
        if (!s_encoder[index].used) {
            s_encoder[index].used = true;
            s_encoder[index].owner = handle;
            s_encoder[index].count = 0;
            handle->base.priv = (void *)(uintptr_t)(index + 1);
            return &s_encoder[index];
        }
    }
    return NULL;
}

port_status_t port_encoder_init(port_encoder_handle_t *handle,
                                const port_encoder_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (sim_encoder_ctx(handle, true) == NULL) {
        return PORT_NO_MEMORY;
    }
    handle->config = *config;
    sim_handle_ready(&handle->base, PORT_STATE_READY);
    return PORT_OK;
}

port_status_t port_encoder_start(port_encoder_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    handle->base.state = PORT_STATE_BUSY;
    return PORT_OK;
}

port_status_t port_encoder_stop(port_encoder_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

int32_t port_encoder_get_count(port_encoder_handle_t *handle)
{
    sim_encoder_ctx_t *ctx;

    if (handle == NULL) {
        return 0;
    }
    ctx = sim_encoder_ctx(handle, false);
    if (ctx == NULL) {
        return 0;
    }
    return handle->config.reverse ? -ctx->count : ctx->count;
}

port_status_t port_encoder_set_count(port_encoder_handle_t *handle, int32_t count)
{
    sim_encoder_ctx_t *ctx;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    ctx = sim_encoder_ctx(handle, false);
    if (ctx == NULL) {
        return PORT_NOT_READY;
    }
    ctx->count = count;
    return PORT_OK;
}

/* ============================== WDG ============================== */

port_status_t port_wdg_init(port_wdg_handle_t *handle, const port_wdg_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->config = *config;
    sim_handle_ready(&handle->base, PORT_STATE_READY);
    return PORT_OK;
}

port_status_t port_wdg_start(port_wdg_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    handle->base.state = PORT_STATE_READY;
    return PORT_OK;
}

port_status_t port_wdg_refresh(port_wdg_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    s_wdg_refresh_count++;
    return PORT_OK;
}

port_status_t port_wdg_stop(port_wdg_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->base.state = PORT_STATE_RESET;
    return PORT_OK;
}

bool port_wdg_is_reset(void)
{
    return s_wdg_reset_flag;
}

void port_wdg_clear_reset_flag(void)
{
    s_wdg_reset_flag = false;
}

/* ============================== RTC ============================== */

static bool sim_rtc_leap(uint32_t year)
{
    return ((year % 4U) == 0U && (year % 100U) != 0U) || ((year % 400U) == 0U);
}

static bool sim_rtc_valid(const port_rtc_time_t *time)
{
    return time->month >= 1U && time->month <= 12U &&
           time->day >= 1U && time->day <= 31U &&
           time->hour < 24U && time->minute < 60U && time->second < 60U;
}

static bool sim_rtc_equal(const port_rtc_time_t *a, const port_rtc_time_t *b)
{
    return a->year == b->year && a->month == b->month && a->day == b->day &&
           a->hour == b->hour && a->minute == b->minute && a->second == b->second;
}

static uint32_t sim_rtc_timestamp(const port_rtc_time_t *time)
{
    static const uint16_t days_before_month[12] = {
        0U, 31U, 59U, 90U, 120U, 151U, 181U, 212U, 243U, 273U, 304U, 334U
    };
    int32_t year = (int32_t)time->year;
    uint32_t month = time->month;
    uint32_t day = time->day;
    int64_t days = 0;
    int32_t y;

    if (year < 1970) {
        return 0U;
    }
    if (month < 1U) {
        month = 1U;
    } else if (month > 12U) {
        month = 12U;
    }
    if (day < 1U) {
        day = 1U;
    } else if (day > 31U) {
        day = 31U;
    }
    for (y = 1970; y < year; y++) {
        days += sim_rtc_leap((uint32_t)y) ? 366 : 365;
    }
    days += days_before_month[month - 1U];
    if (month > 2U && sim_rtc_leap((uint32_t)year)) {
        days += 1;
    }
    days += (int64_t)(day - 1U);
    return (uint32_t)(days * 86400 +
                      (int64_t)time->hour * 3600 +
                      (int64_t)time->minute * 60 +
                      (int64_t)time->second);
}

port_status_t port_rtc_init(void)
{
    return PORT_OK;
}

port_status_t port_rtc_deinit(void)
{
    return PORT_OK;
}

port_status_t port_rtc_set_time(const port_rtc_time_t *time)
{
    if (time == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!sim_rtc_valid(time)) {
        return PORT_INVALID_PARAM;
    }
    s_rtc_time = *time;
    return PORT_OK;
}

port_status_t port_rtc_get_time(port_rtc_time_t *time)
{
    uint32_t i;

    if (time == NULL) {
        return PORT_INVALID_PARAM;
    }
    *time = s_rtc_time;
    for (i = 0U; i < PORT_ARRAY_SIZE(s_rtc_alarm); i++) {
        sim_rtc_alarm_t *alarm = &s_rtc_alarm[i];
        if (alarm->enabled && !alarm->fired && sim_rtc_equal(&alarm->time, &s_rtc_time)) {
            alarm->fired = true;
            if (alarm->callback != NULL) {
                alarm->callback((port_rtc_alarm_t)i, alarm->user_data);
            }
        }
    }
    return PORT_OK;
}

port_status_t port_rtc_set_alarm(port_rtc_alarm_t alarm, const port_rtc_alarm_config_t *config)
{
    sim_rtc_alarm_t *ctx;

    if (config == NULL) {
        return PORT_INVALID_PARAM;
    }
    if ((uint32_t)alarm >= PORT_ARRAY_SIZE(s_rtc_alarm)) {
        return PORT_INVALID_PARAM;
    }
    ctx = &s_rtc_alarm[alarm];
    ctx->enabled = true;
    ctx->fired = false;
    ctx->time = config->time;
    ctx->callback = config->callback;
    ctx->user_data = config->user_data;
    return PORT_OK;
}

port_status_t port_rtc_disable_alarm(port_rtc_alarm_t alarm)
{
    if ((uint32_t)alarm >= PORT_ARRAY_SIZE(s_rtc_alarm)) {
        return PORT_INVALID_PARAM;
    }
    s_rtc_alarm[alarm].enabled = false;
    return PORT_OK;
}

port_status_t port_rtc_get_timestamp(uint32_t *timestamp)
{
    if (timestamp == NULL) {
        return PORT_INVALID_PARAM;
    }
    *timestamp = sim_rtc_timestamp(&s_rtc_time);
    return PORT_OK;
}

/* ============================== FLASH ============================== */

static void sim_flash_ensure(void)
{
    if (!s_flash_ready) {
        memset(s_flash, 0xFF, sizeof(s_flash));
        s_flash_ready = true;
    }
}

port_status_t port_flash_init(port_flash_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    sim_flash_ensure();
    handle->info.start = 0U;
    handle->info.size = SIM_FLASH_SIZE;
    handle->info.erase_size = SIM_FLASH_ERASE;
    handle->info.program_size = 1U;
    handle->info.flags = PORT_FLASH_FLAG_READ | PORT_FLASH_FLAG_WRITE | PORT_FLASH_FLAG_ERASE;
    sim_handle_ready(&handle->base, PORT_STATE_READY);
    return PORT_OK;
}

port_status_t port_flash_deinit(port_flash_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    sim_handle_deinit(&handle->base);
    return PORT_OK;
}

port_status_t port_flash_get_info(port_flash_handle_t *handle, port_flash_info_t *info)
{
    if (handle == NULL || info == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    *info = handle->info;
    return PORT_OK;
}

port_status_t port_flash_read(port_flash_handle_t *handle, uintptr_t address,
                              void *buffer, size_t length)
{
    if (handle == NULL || (buffer == NULL && length > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    if (address > (uintptr_t)SIM_FLASH_SIZE ||
        length > (size_t)((uintptr_t)SIM_FLASH_SIZE - address)) {
        return PORT_INVALID_PARAM;
    }
    sim_flash_ensure();
    if (length > 0U) {
        memcpy(buffer, &s_flash[address], length);
    }
    return PORT_OK;
}

port_status_t port_flash_write(port_flash_handle_t *handle, uintptr_t address,
                               const void *buffer, size_t length)
{
    if (handle == NULL || (buffer == NULL && length > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    if (address > (uintptr_t)SIM_FLASH_SIZE ||
        length > (size_t)((uintptr_t)SIM_FLASH_SIZE - address)) {
        return PORT_INVALID_PARAM;
    }
    sim_flash_ensure();
    if (length > 0U) {
        memcpy(&s_flash[address], buffer, length);
    }
    return PORT_OK;
}

port_status_t port_flash_erase(port_flash_handle_t *handle, uintptr_t address, size_t length)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    if (address > (uintptr_t)SIM_FLASH_SIZE ||
        length > (size_t)((uintptr_t)SIM_FLASH_SIZE - address)) {
        return PORT_INVALID_PARAM;
    }
    sim_flash_ensure();
    if (length > 0U) {
        memset(&s_flash[address], 0xFF, length);
    }
    return PORT_OK;
}

port_status_t port_flash_lock(port_flash_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    return PORT_OK;
}

port_status_t port_flash_unlock(port_flash_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    return PORT_OK;
}

/* ============================== CRC ============================== */

static sim_crc_ctx_t *sim_crc_ctx(port_crc_handle_t *handle, bool create)
{
    int index;

    if (handle == NULL) {
        return NULL;
    }
    index = sim_priv_index(handle->base.priv);
    if (index >= 0 && index < SIM_CRC_MAX && s_crc[index].used &&
        s_crc[index].owner == handle) {
        return &s_crc[index];
    }
    if (!create) {
        return NULL;
    }
    for (index = 0; index < SIM_CRC_MAX; index++) {
        if (!s_crc[index].used) {
            s_crc[index].used = true;
            s_crc[index].owner = handle;
            s_crc[index].state = 0U;
            handle->base.priv = (void *)(uintptr_t)(index + 1);
            return &s_crc[index];
        }
    }
    return NULL;
}

static uint32_t sim_crc_reverse_bits(uint32_t value, uint32_t width)
{
    uint32_t result = 0U;
    uint32_t i;

    for (i = 0U; i < width; i++) {
        result = (result << 1) | (value & 1U);
        value >>= 1;
    }
    return result;
}

static uint32_t sim_crc_compute(const port_crc_config_t *config,
                                const uint8_t *data, size_t length)
{
    uint32_t width = (uint32_t)config->width;
    uint32_t mask = (width >= 32U) ? 0xFFFFFFFFU : ((1UL << width) - 1UL);
    uint32_t top = (width >= 32U) ? 0x80000000U : (uint32_t)(1UL << (width - 1U));
    uint32_t crc = config->initial_value & mask;
    size_t i;

    for (i = 0U; i < length; i++) {
        uint32_t byte = data[i];
        uint32_t bit;

        if (config->input_reverse) {
            byte = sim_crc_reverse_bits(byte, 8U);
        }
        crc ^= (byte << (width - 8U)) & mask;
        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & top) != 0U) {
                crc = ((crc << 1) ^ config->polynomial) & mask;
            } else {
                crc = (crc << 1) & mask;
            }
        }
    }
    if (config->output_reverse) {
        crc = sim_crc_reverse_bits(crc, width);
    }
    return (crc ^ config->xor_output) & mask;
}

port_status_t port_crc_init(port_crc_handle_t *handle, const port_crc_config_t *config)
{
    sim_crc_ctx_t *ctx;

    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (config->width != PORT_CRC_WIDTH_8 &&
        config->width != PORT_CRC_WIDTH_16 &&
        config->width != PORT_CRC_WIDTH_32) {
        return PORT_INVALID_PARAM;
    }
    ctx = sim_crc_ctx(handle, true);
    if (ctx == NULL) {
        return PORT_NO_MEMORY;
    }
    handle->config = *config;
    ctx->state = config->initial_value;
    sim_handle_ready(&handle->base, PORT_STATE_READY);
    return PORT_OK;
}

port_status_t port_crc_reset(port_crc_handle_t *handle)
{
    sim_crc_ctx_t *ctx;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    ctx = sim_crc_ctx(handle, false);
    if (ctx == NULL) {
        return PORT_NOT_READY;
    }
    ctx->state = handle->config.initial_value;
    return PORT_OK;
}

port_status_t port_crc_calculate(port_crc_handle_t *handle, const void *data,
                                 size_t length, uint32_t *result)
{
    sim_crc_ctx_t *ctx;

    if (handle == NULL || result == NULL || (data == NULL && length > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    ctx = sim_crc_ctx(handle, false);
    if (ctx == NULL) {
        return PORT_NOT_READY;
    }
    ctx->state = sim_crc_compute(&handle->config, (const uint8_t *)data, length);
    *result = ctx->state;
    return PORT_OK;
}

port_status_t port_crc_deinit(port_crc_handle_t *handle)
{
    int index;

    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    index = sim_priv_index(handle->base.priv);
    if (index >= 0 && index < SIM_CRC_MAX && s_crc[index].owner == handle) {
        memset(&s_crc[index], 0, sizeof(s_crc[index]));
    }
    handle->base.priv = NULL;
    sim_handle_deinit(&handle->base);
    return PORT_OK;
}

/* ============================== RNG ============================== */

static uint32_t sim_rng_next(void)
{
    s_rng_state = s_rng_state * 1664525U + 1013904223U;
    return s_rng_state;
}

port_status_t port_rng_init(port_rng_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    s_rng_state = SIM_RNG_SEED;
    sim_handle_ready(&handle->base, PORT_STATE_READY);
    return PORT_OK;
}

port_status_t port_rng_deinit(port_rng_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    sim_handle_deinit(&handle->base);
    return PORT_OK;
}

port_status_t port_rng_get(port_rng_handle_t *handle, uint32_t *value)
{
    if (handle == NULL || value == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    *value = sim_rng_next();
    return PORT_OK;
}

port_status_t port_rng_generate(port_rng_handle_t *handle, void *buffer, size_t length)
{
    uint8_t *out = (uint8_t *)buffer;
    size_t i;

    if (handle == NULL || (buffer == NULL && length > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    for (i = 0U; i < length; i++) {
        if ((i % 4U) == 0U) {
            uint32_t value = sim_rng_next();
            size_t j;
            for (j = 0U; j < 4U && (i + j) < length; j++) {
                out[i + j] = (uint8_t)(value >> (8U * j));
            }
        }
    }
    return PORT_OK;
}

/* ============================== PWR ============================== */

port_status_t port_pwr_init(void)
{
    return PORT_OK;
}

port_status_t port_pwr_enter(const port_pwr_config_t *config)
{
    if (config == NULL) {
        return PORT_INVALID_PARAM;
    }
    s_pwr_status.wakeup_flags = config->wakeup_sources;
    PORT_UNUSED(config->low_power_regulator);
    return PORT_OK;
}

port_status_t port_pwr_get_status(port_pwr_status_t *status)
{
    if (status == NULL) {
        return PORT_INVALID_PARAM;
    }
    *status = s_pwr_status;
    return PORT_OK;
}

port_status_t port_pwr_clear_flags(void)
{
    s_pwr_status.reset_flags = 0U;
    s_pwr_status.wakeup_flags = 0U;
    return PORT_OK;
}

/* ============================== QSPI ============================== */

port_status_t port_qspi_init(port_qspi_handle_t *handle, const port_qspi_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    sim_handle_ready(&handle->base, PORT_STATE_READY);
    return PORT_OK;
}

port_status_t port_qspi_deinit(port_qspi_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    sim_handle_deinit(&handle->base);
    return PORT_OK;
}

port_status_t port_qspi_command(port_qspi_handle_t *handle, const port_qspi_command_t *command)
{
    if (handle == NULL || command == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    return PORT_OK;
}

port_status_t port_qspi_transmit(port_qspi_handle_t *handle, const void *data,
                                 size_t length, uint32_t timeout)
{
    if (handle == NULL || (data == NULL && length > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    PORT_UNUSED(timeout);
    return PORT_OK;
}

port_status_t port_qspi_receive(port_qspi_handle_t *handle, void *data,
                                size_t length, uint32_t timeout)
{
    if (handle == NULL || (data == NULL && length > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    PORT_UNUSED(timeout);
    if (length > 0U) {
        memset(data, 0, length);
    }
    return PORT_OK;
}

port_status_t port_qspi_memory_map(port_qspi_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    s_qspi_mapped = true;
    return PORT_OK;
}

port_status_t port_qspi_memory_unmap(port_qspi_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    s_qspi_mapped = false;
    return PORT_OK;
}

/* ============================== SDIO ============================== */

static bool sim_sdio_range_valid(uint32_t block, size_t block_count)
{
    if (block > SIM_SDIO_BLOCKS) {
        return false;
    }
    return block_count <= (size_t)(SIM_SDIO_BLOCKS - block);
}

port_status_t port_sdio_init(port_sdio_handle_t *handle, const port_sdio_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    sim_handle_ready(&handle->base, PORT_STATE_READY);
    return PORT_OK;
}

port_status_t port_sdio_deinit(port_sdio_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    sim_handle_deinit(&handle->base);
    return PORT_OK;
}

port_status_t port_sdio_read(port_sdio_handle_t *handle, uint32_t block, void *buffer,
                             size_t block_count, uint32_t timeout)
{
    size_t offset;

    if (handle == NULL || (buffer == NULL && block_count > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    if (!sim_sdio_range_valid(block, block_count)) {
        return PORT_INVALID_PARAM;
    }
    PORT_UNUSED(timeout);
    offset = (size_t)block * SIM_SDIO_BLOCK_SIZE;
    if (block_count > 0U) {
        memcpy(buffer, &s_sdio[offset], block_count * SIM_SDIO_BLOCK_SIZE);
    }
    return PORT_OK;
}

port_status_t port_sdio_write(port_sdio_handle_t *handle, uint32_t block,
                              const void *buffer, size_t block_count, uint32_t timeout)
{
    size_t offset;

    if (handle == NULL || (buffer == NULL && block_count > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    if (!sim_sdio_range_valid(block, block_count)) {
        return PORT_INVALID_PARAM;
    }
    PORT_UNUSED(timeout);
    offset = (size_t)block * SIM_SDIO_BLOCK_SIZE;
    if (block_count > 0U) {
        memcpy(&s_sdio[offset], buffer, block_count * SIM_SDIO_BLOCK_SIZE);
    }
    return PORT_OK;
}

port_status_t port_sdio_read_dma(port_sdio_handle_t *handle, uint32_t block,
                                 void *buffer, size_t block_count)
{
    return port_sdio_read(handle, block, buffer, block_count, PORT_TIMEOUT_FOREVER);
}

port_status_t port_sdio_write_dma(port_sdio_handle_t *handle, uint32_t block,
                                  const void *buffer, size_t block_count)
{
    return port_sdio_write(handle, block, buffer, block_count, PORT_TIMEOUT_FOREVER);
}

/* ============================== USB ============================== */

port_status_t port_usb_init(port_usb_handle_t *handle, const port_usb_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    s_usb_started = false;
    s_usb_connected = false;
    sim_handle_ready(&handle->base, PORT_STATE_READY);
    return PORT_OK;
}

port_status_t port_usb_deinit(port_usb_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    s_usb_started = false;
    s_usb_connected = false;
    sim_handle_deinit(&handle->base);
    return PORT_OK;
}

port_status_t port_usb_start(port_usb_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    s_usb_started = true;
    return PORT_OK;
}

port_status_t port_usb_stop(port_usb_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    s_usb_started = false;
    s_usb_connected = false;
    return PORT_OK;
}

port_status_t port_usb_connect(port_usb_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    s_usb_connected = true;
    return PORT_OK;
}

port_status_t port_usb_disconnect(port_usb_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    s_usb_connected = false;
    return PORT_OK;
}

/* ============================== ETH ============================== */

port_status_t port_eth_init(port_eth_handle_t *handle, const port_eth_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return PORT_INVALID_PARAM;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    s_eth_started = false;
    s_eth_len = 0U;
    sim_handle_ready(&handle->base, PORT_STATE_READY);
    return PORT_OK;
}

port_status_t port_eth_deinit(port_eth_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    s_eth_started = false;
    s_eth_len = 0U;
    sim_handle_deinit(&handle->base);
    return PORT_OK;
}

port_status_t port_eth_start(port_eth_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    s_eth_started = true;
    return PORT_OK;
}

port_status_t port_eth_stop(port_eth_handle_t *handle)
{
    if (handle == NULL) {
        return PORT_INVALID_PARAM;
    }
    s_eth_started = false;
    return PORT_OK;
}

port_status_t port_eth_transmit(port_eth_handle_t *handle, const void *data, size_t length)
{
    if (handle == NULL || (data == NULL && length > 0U)) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized || !s_eth_started) {
        return PORT_NOT_READY;
    }
    s_eth_len = PORT_MIN(length, SIM_ETH_FRAME_MAX);
    if (s_eth_len > 0U) {
        memcpy(s_eth_frame, data, s_eth_len);
    }
    return PORT_OK;
}

port_status_t port_eth_receive(port_eth_handle_t *handle, void *buffer,
                               size_t buffer_size, size_t *received_length)
{
    if (handle == NULL || buffer == NULL || received_length == NULL) {
        return PORT_INVALID_PARAM;
    }
    if (!handle->base.initialized) {
        return PORT_NOT_READY;
    }
    PORT_UNUSED(buffer_size);
    *received_length = 0U;
    return PORT_OK;
}

/* ============================== 测试辅助 ============================== */

void port_sim_reset_state(void)
{
    s_tick = 0U;
    s_irq_enabled = true;
    s_reset_called = false;
    s_sleep_called = false;
    s_deepsleep_called = false;
    s_platform_initialized = false;

    memset(s_clock_freq, 0, sizeof(s_clock_freq));
    memset(s_gpio, 0, sizeof(s_gpio));
    memset(s_exti, 0, sizeof(s_exti));

    memset(s_uart, 0, sizeof(s_uart));
    memset(s_i2c_mem, 0, sizeof(s_i2c_mem));
    s_i2c_reg = 0U;

    memset(s_i2s_buf, 0, sizeof(s_i2s_buf));
    s_i2s_len = 0U;

    memset(s_can, 0, sizeof(s_can));

    memset(s_adc_value, 0, sizeof(s_adc_value));
    memset(s_adc_max, 0, sizeof(s_adc_max));
    memset(s_dac_value, 0, sizeof(s_dac_value));

    memset(s_comp, 0, sizeof(s_comp));
    memset(s_tim, 0, sizeof(s_tim));
    memset(s_capture, 0, sizeof(s_capture));
    memset(s_encoder, 0, sizeof(s_encoder));

    s_wdg_refresh_count = 0U;
    s_wdg_reset_flag = false;

    memset(&s_rtc_time, 0, sizeof(s_rtc_time));
    memset(s_rtc_alarm, 0, sizeof(s_rtc_alarm));

    memset(s_flash, 0xFF, sizeof(s_flash));
    s_flash_ready = true;

    memset(s_crc, 0, sizeof(s_crc));

    s_rng_state = SIM_RNG_SEED;

    memset(&s_pwr_status, 0, sizeof(s_pwr_status));

    s_qspi_mapped = false;

    memset(s_sdio, 0, sizeof(s_sdio));

    s_usb_started = false;
    s_usb_connected = false;

    s_eth_started = false;
    s_eth_len = 0U;
    memset(s_eth_frame, 0, sizeof(s_eth_frame));

}

void port_sim_tick_advance(uint32_t ms)
{
    s_tick += ms;
}

uint32_t port_sim_uart_get_tx(port_uart_handle_t *h, uint8_t *buf, uint32_t cap)
{
    sim_uart_ctx_t *ctx;
    uint32_t n;

    if (h == NULL || buf == NULL) {
        return 0U;
    }
    ctx = sim_uart_ctx(h, false);
    if (ctx == NULL) {
        return 0U;
    }
    n = (uint32_t)PORT_MIN((size_t)cap, ctx->tx_len);
    if (n > 0U) {
        memcpy(buf, ctx->tx, n);
    }
    return n;
}

uint32_t port_sim_uart_inject(port_uart_handle_t *h, const uint8_t *data, uint32_t len)
{
    sim_uart_ctx_t *ctx;
    size_t space;
    size_t n;

    if (h == NULL || (data == NULL && len > 0U)) {
        return 0U;
    }
    ctx = sim_uart_ctx(h, true);
    if (ctx == NULL) {
        return 0U;
    }
    space = SIM_UART_BUF_SIZE - ctx->rx_len;
    n = PORT_MIN((size_t)len, space);
    if (n > 0U) {
        memcpy(&ctx->rx[ctx->rx_len], data, n);
        ctx->rx_len += n;
    }
    return (uint32_t)n;
}

void port_sim_adc_set(uint8_t channel, uint32_t value)
{
    if (channel < SIM_ADC_CHANNELS) {
        s_adc_value[channel] = value;
    }
}

void port_sim_adc_set_max(uint8_t channel, uint32_t max)
{
    if (channel < SIM_ADC_CHANNELS) {
        s_adc_max[channel] = max;
    }
}

void port_sim_exti_trigger(port_pin_t pin)
{
    sim_exti_t *exti;

    if (!sim_gpio_valid(pin)) {
        return;
    }
    exti = &s_exti[PORT_PIN_GET_PORT(pin)][PORT_PIN_GET_INDEX(pin)];
    if (exti->initialized && exti->enabled && exti->config.callback != NULL) {
        exti->config.callback(pin, exti->config.user_data);
    }
}

void port_sim_can_inject(port_can_handle_t *h, const port_can_frame_t *frame)
{
    sim_can_ctx_t *ctx;
    uint32_t tail;

    if (h == NULL || frame == NULL) {
        return;
    }
    ctx = sim_can_ctx(h, true);
    if (ctx == NULL) {
        return;
    }
    if (ctx->count >= SIM_CAN_QUEUE) {
        ctx->head = (ctx->head + 1U) % SIM_CAN_QUEUE;
        ctx->count--;
    }
    tail = (ctx->head + ctx->count) % SIM_CAN_QUEUE;
    ctx->queue[tail] = *frame;
    ctx->count++;
    if (h->base.initialized && h->callback != NULL) {
        h->callback(h, PORT_EVENT_RX_COMPLETE, h->user_data);
    }
}

void port_sim_capture_set(port_capture_handle_t *h, uint32_t value)
{
    sim_capture_ctx_t *ctx;

    if (h == NULL) {
        return;
    }
    ctx = sim_capture_ctx(h, true);
    if (ctx != NULL) {
        ctx->value = value;
    }
}

void port_sim_encoder_set(port_encoder_handle_t *h, int32_t count)
{
    sim_encoder_ctx_t *ctx;

    if (h == NULL) {
        return;
    }
    ctx = sim_encoder_ctx(h, true);
    if (ctx != NULL) {
        ctx->count = count;
    }
}

void port_sim_comp_set(port_comp_handle_t *h, bool value)
{
    sim_comp_ctx_t *ctx;

    if (h == NULL) {
        return;
    }
    ctx = sim_comp_ctx(h, true);
    if (ctx != NULL) {
        ctx->value = value;
    }
}

void port_sim_i2c_set_mem(uint16_t addr, uint8_t reg, uint8_t value)
{
    if (addr <= 0xFFU) {
        s_i2c_mem[addr][reg] = value;
    }
}

void port_sim_time_set(const port_rtc_time_t *t)
{
    if (t != NULL) {
        s_rtc_time = *t;
    }
}

uint32_t port_sim_wdg_refresh_count(void)
{
    return s_wdg_refresh_count;
}

bool port_sim_reset_called(void)
{
    return s_reset_called;
}
