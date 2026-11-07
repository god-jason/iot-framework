/*
 * PC 主机平台（Host PC Platform）
 *
 * 该文件在 PC 上以纯软件方式实现 PORT 公共 API，不做运行时 ops 分发。
 * 所有外设状态保存在静态内存中，便于单元测试通过 pc.h 注入数据。
 * 不进行任何动态内存分配。
 *
 * UART 模拟见 pc_uart.c：UART1 映射标准输入输出，UART2..N 为虚拟串口。
 */

#include "pc.h"

#include <string.h>

/* ============================== 常量与私有状态 ============================== */

#define PC_GPIO_PORTS      16U
#define PC_GPIO_PINS       32U


#define PC_CAN_MAX         4
#define PC_CAN_QUEUE       16
#define PC_CAN_FILTERS     16

#define PC_I2S_BUF_SIZE    1024U

#define PC_ADC_CHANNELS    32U
#define PC_DAC_CHANNELS    8U

#define PC_MUTEX_MAX       8
#define PC_SEM_MAX         8

#define PC_I2S_MAX         4
#define PC_TIM_MAX         4
#define PC_CAPTURE_MAX     4
#define PC_ENCODER_MAX     4
#define PC_CRC_MAX         4

#define PC_FLASH_SIZE      (64U * 1024U)
#define PC_FLASH_ERASE     1024U

#define PC_SDIO_BLOCK_SIZE 512U
#define PC_SDIO_BLOCKS     (4U * 1024U * 1024U / PC_SDIO_BLOCK_SIZE)

#define PC_ETH_FRAME_MAX   2048U

/* ------------------------------ GPIO / EXTI ------------------------------ */

typedef struct {
    bool initialized;
    iot_gpio_mode_t mode;
    iot_gpio_pull_t pull;
    iot_gpio_level_t level;
} pc_gpio_t;

static pc_gpio_t s_gpio[PC_GPIO_PORTS][PC_GPIO_PINS];

typedef struct {
    bool initialized;
    bool enabled;
    iot_exti_config_t config;
} pc_exti_t;

static pc_exti_t s_exti[PC_GPIO_PORTS][PC_GPIO_PINS];

/* ------------------------------ I2C ------------------------------ */

static uint8_t s_i2c_mem[256][256];
static uint8_t s_i2c_reg;

/* ------------------------------ I2S ------------------------------ */

static uint8_t s_i2s_buf[PC_I2S_BUF_SIZE];
static size_t s_i2s_len;

/* ------------------------------ CAN ------------------------------ */

typedef struct {
    bool used;
    const void *owner;
    iot_can_frame_t queue[PC_CAN_QUEUE];
    uint32_t head;
    uint32_t count;
    iot_can_filter_t filters[PC_CAN_FILTERS];
    uint32_t filter_count;
} pc_can_ctx_t;

static pc_can_ctx_t s_can[PC_CAN_MAX];

/* ------------------------------ ADC / DAC ------------------------------ */

static uint32_t s_adc_value[PC_ADC_CHANNELS];
static uint32_t s_adc_max[PC_ADC_CHANNELS];
static uint32_t s_dac_value[PC_DAC_CHANNELS];

/* ------------------------------ TIM / CAPTURE / ENCODER ------------------------------ */

typedef struct {
    bool used;
    const void *owner;
    uint32_t counter;
    bool started;
} pc_tim_ctx_t;

static pc_tim_ctx_t s_tim[PC_TIM_MAX];

typedef struct {
    bool used;
    const void *owner;
    uint32_t value;
} pc_capture_ctx_t;

static pc_capture_ctx_t s_capture[PC_CAPTURE_MAX];

typedef struct {
    bool used;
    const void *owner;
    int32_t count;
} pc_encoder_ctx_t;

static pc_encoder_ctx_t s_encoder[PC_ENCODER_MAX];

/* ------------------------------ WDG ------------------------------ */

static uint32_t s_wdg_refresh_count;
static bool s_wdg_reset_flag;

/* ------------------------------ RTC ------------------------------ */

static iot_rtc_time_t s_rtc_time;

typedef struct {
    bool enabled;
    bool fired;
    iot_rtc_time_t time;
    iot_rtc_alarm_callback_t callback;
    void *user_data;
} pc_rtc_alarm_t;

static pc_rtc_alarm_t s_rtc_alarm[2];

/* ------------------------------ FLASH ------------------------------ */

static uint8_t s_flash[PC_FLASH_SIZE];
static bool s_flash_ready;

/* ------------------------------ CRC ------------------------------ */

typedef struct {
    bool used;
    const void *owner;
    uint32_t state;
} pc_crc_ctx_t;

static pc_crc_ctx_t s_crc[PC_CRC_MAX];

/* ------------------------------ RNG ------------------------------ */

#define PC_RNG_SEED 0x12345678U
static uint32_t s_rng_state = PC_RNG_SEED;

/* ------------------------------ PWR ------------------------------ */

static iot_pwr_status_t s_pwr_status;

/* ------------------------------ QSPI ------------------------------ */

static bool s_qspi_mapped;

/* ------------------------------ SDIO ------------------------------ */

static uint8_t s_sdio[PC_SDIO_BLOCK_SIZE * PC_SDIO_BLOCKS];

/* ------------------------------ USB ------------------------------ */

static bool s_usb_started;
static bool s_usb_connected;

/* ------------------------------ ETH ------------------------------ */

static bool s_eth_started;
static uint8_t s_eth_frame[PC_ETH_FRAME_MAX];
static size_t s_eth_len;

/* ============================== 通用辅助 ============================== */

static bool pc_gpio_valid(iot_pin_t pin)
{
    return IOT_PIN_GET_PORT(pin) < PC_GPIO_PORTS &&
           IOT_PIN_GET_INDEX(pin) < PC_GPIO_PINS;
}

static void pc_handle_ready(iot_handle_base_t *base, iot_state_t state)
{
    base->state = state;
    base->error = IOT_OK;
}

static void pc_handle_deinit(iot_handle_base_t *base)
{
    base->state = IOT_STATE_RESET;
    base->error = IOT_OK;
}

/* ============================== 平台注册 ============================== */

static iot_status_t pc_platform_init(void)
{
    return IOT_OK;
}

static iot_status_t pc_platform_deinit(void)
{
    return IOT_OK;
}

iot_status_t iot_init(void)
{
    return pc_platform_init();
}

iot_status_t iot_deinit(void)
{
    return pc_platform_deinit();
}

iot_status_t iot_get_info(iot_mcu_info_t *info)
{
    if (info == NULL) {
        return IOT_INVALID_PARAM;
    }
    memset(info, 0, sizeof(*info));
    info->family = IOT_MCU_PC;
    info->model = 1;
    info->revision = 1;
    info->core_clock = 72000000U;
    info->ahb_clock = 72000000U;
    info->apb1_clock = 36000000U;
    info->apb2_clock = 72000000U;
    info->flash_size = 1024U * 1024U;
    info->sram_size = 512U * 1024U;
    return IOT_OK;
}

/* ============================== 核心 ============================== */

uint32_t iot_get_capabilities(iot_device_type_t type, uint8_t instance)
{
    uint32_t caps;

    switch (type) {
    case IOT_DEVICE_GPIO:
        return (instance < PC_GPIO_PORTS) ? 0U : 0U;
    case IOT_DEVICE_UART:
        caps = IOT_CAP_DMA | IOT_CAP_IT | IOT_CAP_IDLE_DETECT;
        break;
    case IOT_DEVICE_SPI:
        caps = IOT_CAP_DMA | IOT_CAP_IT;
        break;
    case IOT_DEVICE_I2C:
        caps = IOT_CAP_DMA | IOT_CAP_IT;
        break;
    case IOT_DEVICE_CAN:
        caps = IOT_CAP_IT;
        break;
    case IOT_DEVICE_ADC:
        caps = IOT_CAP_DMA | IOT_CAP_IT;
        break;
    case IOT_DEVICE_DAC:
        caps = IOT_CAP_DMA;
        break;
    case IOT_DEVICE_PWM:
        caps = 0U;
        break;
    case IOT_DEVICE_TIMER:
        caps = IOT_CAP_IT;
        break;
    case IOT_DEVICE_FLASH:
        caps = 0U;
        break;
    case IOT_DEVICE_USB:
        caps = 0U;
        break;
    case IOT_DEVICE_ETH:
        caps = IOT_CAP_DMA | IOT_CAP_IT;
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

iot_status_t iot_clock_init(const iot_clock_config_t *config)
{
    if (config == NULL) {
        return IOT_INVALID_PARAM;
    }
    s_clock_freq[IOT_CLOCK_CORE] = (config->pll_frequency != 0U) ?
                                    config->pll_frequency : config->frequency;
    s_clock_freq[IOT_CLOCK_AHB] = s_clock_freq[IOT_CLOCK_CORE];
    s_clock_freq[IOT_CLOCK_APB1] = s_clock_freq[IOT_CLOCK_CORE] / 2U;
    s_clock_freq[IOT_CLOCK_APB2] = s_clock_freq[IOT_CLOCK_CORE];
    s_clock_freq[IOT_CLOCK_APB3] = s_clock_freq[IOT_CLOCK_CORE];
    s_clock_freq[IOT_CLOCK_APB4] = s_clock_freq[IOT_CLOCK_CORE];
    return IOT_OK;
}

uint32_t iot_clock_get_frequency(iot_clock_bus_t bus)
{
    if ((uint32_t)bus >= IOT_ARRAY_SIZE(s_clock_freq)) {
        return 0U;
    }
    return s_clock_freq[bus];
}

iot_status_t iot_clock_enable(void *peripheral)
{
    if (peripheral == NULL) {
        return IOT_INVALID_PARAM;
    }
    return IOT_OK;
}

iot_status_t iot_clock_disable(void *peripheral)
{
    if (peripheral == NULL) {
        return IOT_INVALID_PARAM;
    }
    return IOT_OK;
}

iot_status_t iot_pin_init(const iot_pin_config_t *config)
{
    if (config == NULL || !pc_gpio_valid(config->pin)) {
        return IOT_INVALID_PARAM;
    }
    return IOT_OK;
}

iot_status_t iot_pin_deinit(iot_pin_t pin)
{
    if (!pc_gpio_valid(pin)) {
        return IOT_INVALID_PARAM;
    }
    return IOT_OK;
}

/* ============================== GPIO ============================== */

iot_status_t iot_gpio_init(iot_pin_t pin, const iot_gpio_config_t *config)
{
    pc_gpio_t *gpio;

    if (config == NULL || !pc_gpio_valid(pin)) {
        return IOT_INVALID_PARAM;
    }
    gpio = &s_gpio[IOT_PIN_GET_PORT(pin)][IOT_PIN_GET_INDEX(pin)];
    gpio->initialized = true;
    gpio->mode = config->mode;
    gpio->pull = config->pull;
    gpio->level = (config->mode == IOT_GPIO_MODE_OUTPUT) ?
                  config->initial_level : IOT_GPIO_LOW;
    return IOT_OK;
}

iot_status_t iot_gpio_deinit(iot_pin_t pin)
{
    pc_gpio_t *gpio;

    if (!pc_gpio_valid(pin)) {
        return IOT_INVALID_PARAM;
    }
    gpio = &s_gpio[IOT_PIN_GET_PORT(pin)][IOT_PIN_GET_INDEX(pin)];
    gpio->initialized = false;
    gpio->mode = IOT_GPIO_MODE_INPUT;
    gpio->pull = IOT_GPIO_NOPULL;
    gpio->level = IOT_GPIO_LOW;
    return IOT_OK;
}

iot_status_t iot_gpio_write(iot_pin_t pin, iot_gpio_level_t level)
{
    if (!pc_gpio_valid(pin)) {
        return IOT_INVALID_PARAM;
    }
    if (level != IOT_GPIO_LOW && level != IOT_GPIO_HIGH) {
        return IOT_INVALID_PARAM;
    }
    s_gpio[IOT_PIN_GET_PORT(pin)][IOT_PIN_GET_INDEX(pin)].level = level;
    return IOT_OK;
}

iot_status_t iot_gpio_read(iot_pin_t pin, iot_gpio_level_t *level)
{
    if (level == NULL || !pc_gpio_valid(pin)) {
        return IOT_INVALID_PARAM;
    }
    *level = s_gpio[IOT_PIN_GET_PORT(pin)][IOT_PIN_GET_INDEX(pin)].level;
    return IOT_OK;
}

iot_status_t iot_gpio_toggle(iot_pin_t pin)
{
    pc_gpio_t *gpio;

    if (!pc_gpio_valid(pin)) {
        return IOT_INVALID_PARAM;
    }
    gpio = &s_gpio[IOT_PIN_GET_PORT(pin)][IOT_PIN_GET_INDEX(pin)];
    gpio->level = (gpio->level == IOT_GPIO_HIGH) ? IOT_GPIO_LOW : IOT_GPIO_HIGH;
    return IOT_OK;
}

iot_status_t iot_gpio_set(iot_pin_t pin)
{
    return iot_gpio_write(pin, IOT_GPIO_HIGH);
}

iot_status_t iot_gpio_reset(iot_pin_t pin)
{
    return iot_gpio_write(pin, IOT_GPIO_LOW);
}

/* ============================== EXTI ============================== */

iot_status_t iot_exti_init(iot_pin_t pin, const iot_exti_config_t *config)
{
    pc_exti_t *exti;

    if (config == NULL || !pc_gpio_valid(pin)) {
        return IOT_INVALID_PARAM;
    }
    exti = &s_exti[IOT_PIN_GET_PORT(pin)][IOT_PIN_GET_INDEX(pin)];
    exti->config = *config;
    exti->initialized = true;
    exti->enabled = true;
    return IOT_OK;
}

iot_status_t iot_exti_enable(iot_pin_t pin)
{
    pc_exti_t *exti;

    if (!pc_gpio_valid(pin)) {
        return IOT_INVALID_PARAM;
    }
    exti = &s_exti[IOT_PIN_GET_PORT(pin)][IOT_PIN_GET_INDEX(pin)];
    if (!exti->initialized) {
        return IOT_NOT_READY;
    }
    exti->enabled = true;
    return IOT_OK;
}

iot_status_t iot_exti_disable(iot_pin_t pin)
{
    pc_exti_t *exti;

    if (!pc_gpio_valid(pin)) {
        return IOT_INVALID_PARAM;
    }
    exti = &s_exti[IOT_PIN_GET_PORT(pin)][IOT_PIN_GET_INDEX(pin)];
    exti->enabled = false;
    return IOT_OK;
}

iot_status_t iot_exti_clear(iot_pin_t pin)
{
    if (!pc_gpio_valid(pin)) {
        return IOT_INVALID_PARAM;
    }
    return IOT_OK;
}

iot_status_t iot_exti_deinit(iot_pin_t pin)
{
    pc_exti_t *exti;

    if (!pc_gpio_valid(pin)) {
        return IOT_INVALID_PARAM;
    }
    exti = &s_exti[IOT_PIN_GET_PORT(pin)][IOT_PIN_GET_INDEX(pin)];
    memset(exti, 0, sizeof(*exti));
    return IOT_OK;
}

/* ============================== DMA ============================== */

iot_status_t iot_dma_init(iot_dma_t *handle, const iot_dma_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    pc_handle_ready(&handle->base, IOT_STATE_READY);
    return IOT_OK;
}

iot_status_t iot_dma_deinit(iot_dma_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    pc_handle_deinit(&handle->base);
    return IOT_OK;
}

iot_status_t iot_dma_start(iot_dma_t *handle, uintptr_t src, uintptr_t dst, size_t length)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    if (handle->config.direction != IOT_DMA_MEMORY_TO_MEMORY) {
        return IOT_NOT_SUPPORTED;
    }
    if (length > 0U) {
        if (src == 0U || dst == 0U) {
            return IOT_INVALID_PARAM;
        }
        memcpy((void *)dst, (const void *)src, length);
    }
    handle->base.state = IOT_STATE_READY;
    if (handle->callback != NULL) {
        handle->callback(handle, IOT_EVENT_COMPLETE, handle->user_data);
    }
    return IOT_OK;
}

iot_status_t iot_dma_stop(iot_dma_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->base.state = IOT_STATE_READY;
    return IOT_OK;
}

iot_status_t iot_dma_suspend(iot_dma_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->base.state = IOT_STATE_SUSPENDED;
    return IOT_OK;
}

iot_status_t iot_dma_resume(iot_dma_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->base.state = IOT_STATE_READY;
    return IOT_OK;
}

size_t iot_dma_get_remaining(iot_dma_t *handle)
{
    IOT_UNUSED(handle);
    return 0U;
}

/* ============================== SPI ============================== */

iot_status_t iot_spi_init(iot_spi_t *handle, const iot_spi_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    pc_handle_ready(&handle->base, IOT_STATE_READY);
    return IOT_OK;
}

iot_status_t iot_spi_deinit(iot_spi_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    pc_handle_deinit(&handle->base);
    return IOT_OK;
}

iot_status_t iot_spi_transmit(iot_spi_t *handle, const void *data,
                                size_t length, uint32_t timeout)
{
    if (handle == NULL || (data == NULL && length > 0U)) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    IOT_UNUSED(timeout);
    return IOT_OK;
}

iot_status_t iot_spi_receive(iot_spi_t *handle, void *data,
                               size_t length, uint32_t timeout)
{
    if (handle == NULL || (data == NULL && length > 0U)) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    IOT_UNUSED(timeout);
    if (length > 0U) {
        memset(data, 0, length);
    }
    return IOT_OK;
}

iot_status_t iot_spi_transfer(iot_spi_t *handle, const void *tx_data,
                                void *rx_data, size_t length, uint32_t timeout)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    if (length == 0U) {
        return IOT_OK;
    }
    if (tx_data == NULL || rx_data == NULL) {
        return IOT_INVALID_PARAM;
    }
    IOT_UNUSED(timeout);
    memcpy(rx_data, tx_data, length);
    return IOT_OK;
}

iot_status_t iot_spi_transmit_dma(iot_spi_t *handle, const void *data, size_t length)
{
    return iot_spi_transmit(handle, data, length, IOT_TIMEOUT_FOREVER);
}

iot_status_t iot_spi_receive_dma(iot_spi_t *handle, void *data, size_t length)
{
    return iot_spi_receive(handle, data, length, IOT_TIMEOUT_FOREVER);
}

iot_status_t iot_spi_transfer_dma(iot_spi_t *handle, const void *tx_data,
                                    void *rx_data, size_t length)
{
    return iot_spi_transfer(handle, tx_data, rx_data, length, IOT_TIMEOUT_FOREVER);
}

iot_status_t iot_spi_set_speed(iot_spi_t *handle, uint32_t baudrate)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    handle->config.baudrate = baudrate;
    return IOT_OK;
}

iot_status_t iot_spi_abort(iot_spi_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->base.state = IOT_STATE_READY;
    return IOT_OK;
}

iot_state_t iot_spi_get_state(iot_spi_t *handle)
{
    if (handle == NULL) {
        return IOT_STATE_RESET;
    }
    return handle->base.state;
}

/* ============================== I2C ============================== */

static bool pc_i2c_addr_valid(uint16_t address)
{
    return address <= 0xFFU;
}

iot_status_t iot_i2c_init(iot_i2c_t *handle, const iot_i2c_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    pc_handle_ready(&handle->base, IOT_STATE_READY);
    return IOT_OK;
}

iot_status_t iot_i2c_deinit(iot_i2c_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    pc_handle_deinit(&handle->base);
    return IOT_OK;
}

iot_status_t iot_i2c_master_transmit(iot_i2c_t *handle, uint16_t address,
                                       const uint8_t *data, size_t length, uint32_t timeout)
{
    size_t i;

    if (handle == NULL || (data == NULL && length > 0U)) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    if (!pc_i2c_addr_valid(address)) {
        return IOT_INVALID_PARAM;
    }
    IOT_UNUSED(timeout);
    if (length == 0U) {
        return IOT_OK;
    }
    s_i2c_reg = data[0];
    for (i = 1U; i < length; i++) {
        s_i2c_mem[address][s_i2c_reg & 0xFFU] = data[i];
        s_i2c_reg = (uint8_t)((s_i2c_reg + 1U) & 0xFFU);
    }
    return IOT_OK;
}

iot_status_t iot_i2c_master_receive(iot_i2c_t *handle, uint16_t address,
                                      uint8_t *data, size_t length, uint32_t timeout)
{
    size_t i;

    if (handle == NULL || (data == NULL && length > 0U)) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    if (!pc_i2c_addr_valid(address)) {
        return IOT_INVALID_PARAM;
    }
    IOT_UNUSED(timeout);
    for (i = 0U; i < length; i++) {
        data[i] = s_i2c_mem[address][s_i2c_reg & 0xFFU];
        s_i2c_reg = (uint8_t)((s_i2c_reg + 1U) & 0xFFU);
    }
    return IOT_OK;
}

iot_status_t iot_i2c_master_transfer(iot_i2c_t *handle, uint16_t address,
                                       const uint8_t *tx_data, size_t tx_length,
                                       uint8_t *rx_data, size_t rx_length, uint32_t timeout)
{
    iot_status_t status;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    if (tx_length > 0U && tx_data == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (rx_length > 0U && rx_data == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (tx_length > 0U) {
        status = iot_i2c_master_transmit(handle, address, tx_data, tx_length, timeout);
        if (status != IOT_OK) {
            return status;
        }
    }
    if (rx_length > 0U) {
        return iot_i2c_master_receive(handle, address, rx_data, rx_length, timeout);
    }
    return IOT_OK;
}

iot_status_t iot_i2c_mem_write(iot_i2c_t *handle, uint16_t address,
                                 uint16_t reg, uint8_t reg_size,
                                 const uint8_t *data, size_t length, uint32_t timeout)
{
    size_t i;

    if (handle == NULL || (data == NULL && length > 0U)) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    if (reg_size != 1U && reg_size != 2U) {
        return IOT_INVALID_PARAM;
    }
    if (!pc_i2c_addr_valid(address)) {
        return IOT_INVALID_PARAM;
    }
    IOT_UNUSED(timeout);
    s_i2c_reg = (uint8_t)(reg & 0xFFU);
    for (i = 0U; i < length; i++) {
        s_i2c_mem[address][s_i2c_reg & 0xFFU] = data[i];
        s_i2c_reg = (uint8_t)((s_i2c_reg + 1U) & 0xFFU);
    }
    return IOT_OK;
}

iot_status_t iot_i2c_mem_read(iot_i2c_t *handle, uint16_t address,
                                uint16_t reg, uint8_t reg_size,
                                uint8_t *data, size_t length, uint32_t timeout)
{
    size_t i;

    if (handle == NULL || (data == NULL && length > 0U)) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    if (reg_size != 1U && reg_size != 2U) {
        return IOT_INVALID_PARAM;
    }
    if (!pc_i2c_addr_valid(address)) {
        return IOT_INVALID_PARAM;
    }
    IOT_UNUSED(timeout);
    s_i2c_reg = (uint8_t)(reg & 0xFFU);
    for (i = 0U; i < length; i++) {
        data[i] = s_i2c_mem[address][s_i2c_reg & 0xFFU];
        s_i2c_reg = (uint8_t)((s_i2c_reg + 1U) & 0xFFU);
    }
    return IOT_OK;
}

iot_status_t iot_i2c_master_transmit_dma(iot_i2c_t *handle, uint16_t address,
                                           const uint8_t *data, size_t length)
{
    return iot_i2c_master_transmit(handle, address, data, length, IOT_TIMEOUT_FOREVER);
}

iot_status_t iot_i2c_master_receive_dma(iot_i2c_t *handle, uint16_t address,
                                          uint8_t *data, size_t length)
{
    return iot_i2c_master_receive(handle, address, data, length, IOT_TIMEOUT_FOREVER);
}

iot_status_t iot_i2c_is_device_ready(iot_i2c_t *handle, uint16_t address,
                                       uint32_t trials, uint32_t timeout)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    IOT_UNUSED(trials);
    IOT_UNUSED(timeout);
    return (address <= 0x7FU) ? IOT_OK : IOT_NOT_FOUND;
}

iot_status_t iot_i2c_bus_recover(iot_i2c_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->base.state = IOT_STATE_READY;
    return IOT_OK;
}

iot_status_t iot_i2c_abort(iot_i2c_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->base.state = IOT_STATE_READY;
    return IOT_OK;
}

iot_state_t iot_i2c_get_state(iot_i2c_t *handle)
{
    if (handle == NULL) {
        return IOT_STATE_RESET;
    }
    return handle->base.state;
}

/* ============================== I2S ============================== */

iot_status_t iot_i2s_init(iot_i2s_t *handle, const iot_i2s_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    pc_handle_ready(&handle->base, IOT_STATE_READY);
    return IOT_OK;
}

iot_status_t iot_i2s_deinit(iot_i2s_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    pc_handle_deinit(&handle->base);
    return IOT_OK;
}

iot_status_t iot_i2s_transmit(iot_i2s_t *handle, const void *data,
                                size_t length, uint32_t timeout)
{
    size_t n;

    if (handle == NULL || (data == NULL && length > 0U)) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    IOT_UNUSED(timeout);
    n = IOT_MIN(length, PC_I2S_BUF_SIZE);
    if (n > 0U) {
        memcpy(s_i2s_buf, data, n);
    }
    s_i2s_len = n;
    return IOT_OK;
}

iot_status_t iot_i2s_receive(iot_i2s_t *handle, void *data,
                               size_t length, uint32_t timeout)
{
    size_t n;

    if (handle == NULL || (data == NULL && length > 0U)) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    IOT_UNUSED(timeout);
    n = IOT_MIN(length, s_i2s_len);
    if (n > 0U) {
        memcpy(data, s_i2s_buf, n);
    }
    if (length > n) {
        memset((uint8_t *)data + n, 0, length - n);
    }
    return IOT_OK;
}

iot_status_t iot_i2s_transmit_dma(iot_i2s_t *handle, const void *data, size_t length)
{
    return iot_i2s_transmit(handle, data, length, IOT_TIMEOUT_FOREVER);
}

iot_status_t iot_i2s_receive_dma(iot_i2s_t *handle, void *data, size_t length)
{
    return iot_i2s_receive(handle, data, length, IOT_TIMEOUT_FOREVER);
}

iot_status_t iot_i2s_abort(iot_i2s_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->base.state = IOT_STATE_READY;
    return IOT_OK;
}

/* ============================== CAN ============================== */

static pc_can_ctx_t *pc_can_ctx(iot_can_t *handle, bool create)
{
    int index;

    if (handle == NULL) {
        return NULL;
    }
    for (index = 0; index < PC_CAN_MAX; index++) {
        if (s_can[index].used && s_can[index].owner == handle) {
            return &s_can[index];
        }
    }
    if (!create) {
        return NULL;
    }
    for (index = 0; index < PC_CAN_MAX; index++) {
        if (!s_can[index].used) {
            memset(&s_can[index], 0, sizeof(s_can[index]));
            s_can[index].used = true;
            s_can[index].owner = handle;
            return &s_can[index];
        }
    }
    return NULL;
}

iot_status_t iot_can_init(iot_can_t *handle, const iot_can_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (pc_can_ctx(handle, true) == NULL) {
        return IOT_NO_MEMORY;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    pc_handle_ready(&handle->base, IOT_STATE_READY);
    return IOT_OK;
}

iot_status_t iot_can_deinit(iot_can_t *handle)
{
    int index;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    for (index = 0; index < PC_CAN_MAX; index++) {
        if (s_can[index].used && s_can[index].owner == handle) {
            memset(&s_can[index], 0, sizeof(s_can[index]));
            break;
        }
    }
    pc_handle_deinit(&handle->base);
    return IOT_OK;
}

iot_status_t iot_can_start(iot_can_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    handle->base.state = IOT_STATE_READY;
    return IOT_OK;
}

iot_status_t iot_can_stop(iot_can_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->base.state = IOT_STATE_RESET;
    return IOT_OK;
}

iot_status_t iot_can_transmit(iot_can_t *handle, const iot_can_frame_t *frame,
                                uint32_t timeout)
{
    if (handle == NULL || frame == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    IOT_UNUSED(timeout);
    if ((handle->config.mode == IOT_CAN_MODE_LOOPBACK ||
         handle->config.mode == IOT_CAN_MODE_SILENT_LOOPBACK) &&
        handle->callback != NULL) {
        handle->callback(handle, IOT_EVENT_RX_COMPLETE, handle->user_data);
    }
    return IOT_OK;
}

iot_status_t iot_can_receive(iot_can_t *handle, iot_can_frame_t *frame,
                               uint32_t timeout)
{
    pc_can_ctx_t *ctx;

    if (handle == NULL || frame == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    IOT_UNUSED(timeout);
    ctx = pc_can_ctx(handle, false);
    if (ctx == NULL || ctx->count == 0U) {
        return IOT_TIMEOUT;
    }
    *frame = ctx->queue[ctx->head];
    ctx->head = (ctx->head + 1U) % PC_CAN_QUEUE;
    ctx->count--;
    return IOT_OK;
}

iot_status_t iot_can_transmit_it(iot_can_t *handle, const iot_can_frame_t *frame)
{
    return iot_can_transmit(handle, frame, IOT_TIMEOUT_FOREVER);
}

iot_status_t iot_can_add_filter(iot_can_t *handle, const iot_can_filter_t *filter)
{
    pc_can_ctx_t *ctx;

    if (handle == NULL || filter == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    ctx = pc_can_ctx(handle, false);
    if (ctx == NULL) {
        return IOT_NOT_READY;
    }
    if (ctx->filter_count >= PC_CAN_FILTERS) {
        return IOT_NO_MEMORY;
    }
    ctx->filters[ctx->filter_count] = *filter;
    ctx->filter_count++;
    return IOT_OK;
}

iot_status_t iot_can_clear_filters(iot_can_t *handle)
{
    pc_can_ctx_t *ctx;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    ctx = pc_can_ctx(handle, false);
    if (ctx != NULL) {
        ctx->filter_count = 0U;
    }
    return IOT_OK;
}

/* ============================== ADC ============================== */

iot_status_t iot_adc_init(iot_adc_t *handle, const iot_adc_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    pc_handle_ready(&handle->base, IOT_STATE_READY);
    return IOT_OK;
}

iot_status_t iot_adc_deinit(iot_adc_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    pc_handle_deinit(&handle->base);
    return IOT_OK;
}

iot_status_t iot_adc_config_channel(iot_adc_t *handle,
                                      const iot_adc_channel_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    if (config->channel >= PC_ADC_CHANNELS) {
        return IOT_INVALID_PARAM;
    }
    return IOT_OK;
}

iot_status_t iot_adc_start(iot_adc_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    handle->base.state = IOT_STATE_READY;
    return IOT_OK;
}

iot_status_t iot_adc_stop(iot_adc_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->base.state = IOT_STATE_READY;
    return IOT_OK;
}

iot_status_t iot_adc_read(iot_adc_t *handle, uint8_t channel,
                            uint32_t *value, uint32_t timeout)
{
    uint32_t max_value;

    if (handle == NULL || value == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    if (channel >= PC_ADC_CHANNELS) {
        return IOT_INVALID_PARAM;
    }
    IOT_UNUSED(timeout);
    max_value = (uint32_t)((1UL << (uint32_t)handle->config.resolution) - 1UL);
    if (s_adc_max[channel] != 0U && s_adc_max[channel] < max_value) {
        max_value = s_adc_max[channel];
    }
    *value = IOT_MIN(s_adc_value[channel], max_value);
    return IOT_OK;
}

iot_status_t iot_adc_start_it(iot_adc_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    if (handle->callback != NULL) {
        handle->callback(handle, IOT_EVENT_COMPLETE, handle->user_data);
    }
    return IOT_OK;
}

iot_status_t iot_adc_start_dma(iot_adc_t *handle, uint32_t *buffer, size_t length)
{
    uint32_t max_value;
    size_t i;

    if (handle == NULL || (buffer == NULL && length > 0U)) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    max_value = (uint32_t)((1UL << (uint32_t)handle->config.resolution) - 1UL);
    for (i = 0U; i < length; i++) {
        uint32_t channel = (uint32_t)(i % PC_ADC_CHANNELS);
        uint32_t limit = max_value;
        if (s_adc_max[channel] != 0U && s_adc_max[channel] < limit) {
            limit = s_adc_max[channel];
        }
        buffer[i] = IOT_MIN(s_adc_value[channel], limit);
    }
    if (handle->callback != NULL) {
        handle->callback(handle, IOT_EVENT_COMPLETE, handle->user_data);
    }
    return IOT_OK;
}

iot_status_t iot_adc_calibrate(iot_adc_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    return IOT_OK;
}

uint32_t iot_adc_get_max_value(iot_adc_t *handle)
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

iot_status_t iot_dac_init(iot_dac_t *handle, const iot_dac_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->config = *config;
    pc_handle_ready(&handle->base, IOT_STATE_READY);
    return IOT_OK;
}

iot_status_t iot_dac_deinit(iot_dac_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    pc_handle_deinit(&handle->base);
    return IOT_OK;
}

iot_status_t iot_dac_start(iot_dac_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    handle->base.state = IOT_STATE_READY;
    return IOT_OK;
}

iot_status_t iot_dac_stop(iot_dac_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->base.state = IOT_STATE_READY;
    return IOT_OK;
}

iot_status_t iot_dac_write(iot_dac_t *handle, uint32_t value)
{
    uint8_t channel;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    channel = handle->config.channel;
    if (channel >= PC_DAC_CHANNELS) {
        channel = 0U;
    }
    s_dac_value[channel] = (value > 4095U) ? 4095U : value;
    return IOT_OK;
}

iot_status_t iot_dac_write_dma(iot_dac_t *handle, const uint32_t *buffer, size_t length)
{
    size_t i;

    if (handle == NULL || (buffer == NULL && length > 0U)) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    for (i = 0U; i < length; i++) {
        iot_status_t status = iot_dac_write(handle, buffer[i]);
        if (status != IOT_OK) {
            return status;
        }
    }
    return IOT_OK;
}

/* ============================== OPAMP ============================== */

iot_status_t iot_opamp_init(iot_opamp_t *handle, const iot_opamp_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->config = *config;
    pc_handle_ready(&handle->base, IOT_STATE_READY);
    return IOT_OK;
}

iot_status_t iot_opamp_start(iot_opamp_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    handle->base.state = IOT_STATE_READY;
    return IOT_OK;
}

iot_status_t iot_opamp_stop(iot_opamp_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->base.state = IOT_STATE_READY;
    return IOT_OK;
}

/* ============================== TIM ============================== */

static pc_tim_ctx_t *pc_tim_ctx(iot_tim_t *handle, bool create)
{
    int index;

    if (handle == NULL) {
        return NULL;
    }
    for (index = 0; index < PC_TIM_MAX; index++) {
        if (s_tim[index].used && s_tim[index].owner == handle) {
            return &s_tim[index];
        }
    }
    if (!create) {
        return NULL;
    }
    for (index = 0; index < PC_TIM_MAX; index++) {
        if (!s_tim[index].used) {
            s_tim[index].used = true;
            s_tim[index].owner = handle;
            s_tim[index].counter = 0U;
            s_tim[index].started = false;
            return &s_tim[index];
        }
    }
    return NULL;
}

iot_status_t iot_tim_init(iot_tim_t *handle, const iot_tim_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (pc_tim_ctx(handle, true) == NULL) {
        return IOT_NO_MEMORY;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    pc_handle_ready(&handle->base, IOT_STATE_READY);
    return IOT_OK;
}

iot_status_t iot_tim_deinit(iot_tim_t *handle)
{
    int index;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    for (index = 0; index < PC_TIM_MAX; index++) {
        if (s_tim[index].used && s_tim[index].owner == handle) {
            memset(&s_tim[index], 0, sizeof(s_tim[index]));
            break;
        }
    }
    pc_handle_deinit(&handle->base);
    return IOT_OK;
}

iot_status_t iot_tim_start(iot_tim_t *handle)
{
    pc_tim_ctx_t *ctx;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    ctx = pc_tim_ctx(handle, false);
    if (ctx == NULL) {
        return IOT_NOT_READY;
    }
    ctx->started = true;
    handle->base.state = IOT_STATE_BUSY;
    return IOT_OK;
}

iot_status_t iot_tim_stop(iot_tim_t *handle)
{
    pc_tim_ctx_t *ctx;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    ctx = pc_tim_ctx(handle, false);
    if (ctx != NULL) {
        ctx->started = false;
    }
    handle->base.state = IOT_STATE_READY;
    return IOT_OK;
}

iot_status_t iot_tim_start_it(iot_tim_t *handle)
{
    pc_tim_ctx_t *ctx;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    ctx = pc_tim_ctx(handle, false);
    if (ctx == NULL) {
        return IOT_NOT_READY;
    }
    ctx->started = true;
    handle->base.state = IOT_STATE_BUSY;
    if (handle->callback != NULL) {
        handle->callback(handle, IOT_EVENT_COMPLETE, handle->user_data);
    }
    return IOT_OK;
}

iot_status_t iot_tim_stop_it(iot_tim_t *handle)
{
    return iot_tim_stop(handle);
}

iot_status_t iot_tim_set_period(iot_tim_t *handle, uint32_t period)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->config.period = period;
    return IOT_OK;
}

iot_status_t iot_tim_set_prescaler(iot_tim_t *handle, uint32_t prescaler)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->config.prescaler = prescaler;
    return IOT_OK;
}

iot_status_t iot_tim_set_counter(iot_tim_t *handle, uint32_t counter)
{
    pc_tim_ctx_t *ctx;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    ctx = pc_tim_ctx(handle, false);
    if (ctx == NULL) {
        return IOT_NOT_READY;
    }
    ctx->counter = counter;
    return IOT_OK;
}

uint32_t iot_tim_get_counter(iot_tim_t *handle)
{
    pc_tim_ctx_t *ctx;

    if (handle == NULL) {
        return 0U;
    }
    ctx = pc_tim_ctx(handle, false);
    return (ctx != NULL) ? ctx->counter : 0U;
}

/* ============================== PWM ============================== */

iot_status_t iot_pwm_init(iot_pwm_t *handle, const iot_pwm_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (config->duty > 10000U) {
        return IOT_INVALID_PARAM;
    }
    handle->config = *config;
    pc_handle_ready(&handle->base, IOT_STATE_READY);
    return IOT_OK;
}

iot_status_t iot_pwm_deinit(iot_pwm_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    pc_handle_deinit(&handle->base);
    return IOT_OK;
}

iot_status_t iot_pwm_start(iot_pwm_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    handle->base.state = IOT_STATE_READY;
    return IOT_OK;
}

iot_status_t iot_pwm_stop(iot_pwm_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->base.state = IOT_STATE_READY;
    return IOT_OK;
}

iot_status_t iot_pwm_set_frequency(iot_pwm_t *handle, uint32_t frequency)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (frequency == 0U) {
        return IOT_INVALID_PARAM;
    }
    handle->config.frequency = frequency;
    return IOT_OK;
}

iot_status_t iot_pwm_set_duty(iot_pwm_t *handle, uint32_t duty)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (duty > 10000U) {
        return IOT_INVALID_PARAM;
    }
    handle->config.duty = duty;
    return IOT_OK;
}

iot_status_t iot_pwm_set_polarity(iot_pwm_t *handle, iot_pwm_polarity_t polarity)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->config.polarity = polarity;
    return IOT_OK;
}

/* ============================== CAPTURE ============================== */

static pc_capture_ctx_t *pc_capture_ctx(iot_capture_t *handle, bool create)
{
    int index;

    if (handle == NULL) {
        return NULL;
    }
    for (index = 0; index < PC_CAPTURE_MAX; index++) {
        if (s_capture[index].used && s_capture[index].owner == handle) {
            return &s_capture[index];
        }
    }
    if (!create) {
        return NULL;
    }
    for (index = 0; index < PC_CAPTURE_MAX; index++) {
        if (!s_capture[index].used) {
            s_capture[index].used = true;
            s_capture[index].owner = handle;
            s_capture[index].value = 0U;
            return &s_capture[index];
        }
    }
    return NULL;
}

iot_status_t iot_capture_init(iot_capture_t *handle,
                                const iot_capture_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (pc_capture_ctx(handle, true) == NULL) {
        return IOT_NO_MEMORY;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    pc_handle_ready(&handle->base, IOT_STATE_READY);
    return IOT_OK;
}

iot_status_t iot_capture_start(iot_capture_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    handle->base.state = IOT_STATE_BUSY;
    return IOT_OK;
}

iot_status_t iot_capture_stop(iot_capture_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->base.state = IOT_STATE_READY;
    return IOT_OK;
}

iot_status_t iot_capture_start_it(iot_capture_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    if (pc_capture_ctx(handle, false) == NULL) {
        return IOT_NOT_READY;
    }
    handle->base.state = IOT_STATE_BUSY;
    if (handle->callback != NULL) {
        handle->callback(handle, IOT_EVENT_COMPLETE, handle->user_data);
    }
    return IOT_OK;
}

uint32_t iot_capture_get_value(iot_capture_t *handle)
{
    pc_capture_ctx_t *ctx;

    if (handle == NULL) {
        return 0U;
    }
    ctx = pc_capture_ctx(handle, false);
    return (ctx != NULL) ? ctx->value : 0U;
}

/* ============================== ENCODER ============================== */

static pc_encoder_ctx_t *pc_encoder_ctx(iot_encoder_t *handle, bool create)
{
    int index;

    if (handle == NULL) {
        return NULL;
    }
    for (index = 0; index < PC_ENCODER_MAX; index++) {
        if (s_encoder[index].used && s_encoder[index].owner == handle) {
            return &s_encoder[index];
        }
    }
    if (!create) {
        return NULL;
    }
    for (index = 0; index < PC_ENCODER_MAX; index++) {
        if (!s_encoder[index].used) {
            s_encoder[index].used = true;
            s_encoder[index].owner = handle;
            s_encoder[index].count = 0;
            return &s_encoder[index];
        }
    }
    return NULL;
}

iot_status_t iot_encoder_init(iot_encoder_t *handle,
                                const iot_encoder_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (pc_encoder_ctx(handle, true) == NULL) {
        return IOT_NO_MEMORY;
    }
    handle->config = *config;
    pc_handle_ready(&handle->base, IOT_STATE_READY);
    return IOT_OK;
}

iot_status_t iot_encoder_start(iot_encoder_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    handle->base.state = IOT_STATE_BUSY;
    return IOT_OK;
}

iot_status_t iot_encoder_stop(iot_encoder_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->base.state = IOT_STATE_READY;
    return IOT_OK;
}

int32_t iot_encoder_get_count(iot_encoder_t *handle)
{
    pc_encoder_ctx_t *ctx;

    if (handle == NULL) {
        return 0;
    }
    ctx = pc_encoder_ctx(handle, false);
    if (ctx == NULL) {
        return 0;
    }
    return handle->config.reverse ? -ctx->count : ctx->count;
}

iot_status_t iot_encoder_set_count(iot_encoder_t *handle, int32_t count)
{
    pc_encoder_ctx_t *ctx;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    ctx = pc_encoder_ctx(handle, false);
    if (ctx == NULL) {
        return IOT_NOT_READY;
    }
    ctx->count = count;
    return IOT_OK;
}

/* ============================== WDG ============================== */

iot_status_t iot_wdg_init(iot_wdg_t *handle, const iot_wdg_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->config = *config;
    pc_handle_ready(&handle->base, IOT_STATE_READY);
    return IOT_OK;
}

iot_status_t iot_wdg_start(iot_wdg_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    handle->base.state = IOT_STATE_READY;
    return IOT_OK;
}

iot_status_t iot_wdg_refresh(iot_wdg_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    s_wdg_refresh_count++;
    return IOT_OK;
}

iot_status_t iot_wdg_stop(iot_wdg_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->base.state = IOT_STATE_RESET;
    return IOT_OK;
}

bool iot_wdg_is_reset(void)
{
    return s_wdg_reset_flag;
}

void iot_wdg_clear_reset_flag(void)
{
    s_wdg_reset_flag = false;
}

/* ============================== RTC ============================== */

static bool pc_rtc_leap(uint32_t year)
{
    return ((year % 4U) == 0U && (year % 100U) != 0U) || ((year % 400U) == 0U);
}

static bool pc_rtc_valid(const iot_rtc_time_t *time)
{
    return time->month >= 1U && time->month <= 12U &&
           time->day >= 1U && time->day <= 31U &&
           time->hour < 24U && time->minute < 60U && time->second < 60U;
}

static bool pc_rtc_equal(const iot_rtc_time_t *a, const iot_rtc_time_t *b)
{
    return a->year == b->year && a->month == b->month && a->day == b->day &&
           a->hour == b->hour && a->minute == b->minute && a->second == b->second;
}

static uint32_t pc_rtc_timestamp(const iot_rtc_time_t *time)
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
        days += pc_rtc_leap((uint32_t)y) ? 366 : 365;
    }
    days += days_before_month[month - 1U];
    if (month > 2U && pc_rtc_leap((uint32_t)year)) {
        days += 1;
    }
    days += (int64_t)(day - 1U);
    return (uint32_t)(days * 86400 +
                      (int64_t)time->hour * 3600 +
                      (int64_t)time->minute * 60 +
                      (int64_t)time->second);
}

iot_status_t iot_rtc_init(void)
{
    return IOT_OK;
}

iot_status_t iot_rtc_deinit(void)
{
    return IOT_OK;
}

iot_status_t iot_rtc_set_time(const iot_rtc_time_t *time)
{
    if (time == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (!pc_rtc_valid(time)) {
        return IOT_INVALID_PARAM;
    }
    s_rtc_time = *time;
    return IOT_OK;
}

iot_status_t iot_rtc_get_time(iot_rtc_time_t *time)
{
    uint32_t i;

    if (time == NULL) {
        return IOT_INVALID_PARAM;
    }
    *time = s_rtc_time;
    for (i = 0U; i < IOT_ARRAY_SIZE(s_rtc_alarm); i++) {
        pc_rtc_alarm_t *alarm = &s_rtc_alarm[i];
        if (alarm->enabled && !alarm->fired && pc_rtc_equal(&alarm->time, &s_rtc_time)) {
            alarm->fired = true;
            if (alarm->callback != NULL) {
                alarm->callback((iot_rtc_alarm_t)i, alarm->user_data);
            }
        }
    }
    return IOT_OK;
}

iot_status_t iot_rtc_set_alarm(iot_rtc_alarm_t alarm, const iot_rtc_alarm_config_t *config)
{
    pc_rtc_alarm_t *ctx;

    if (config == NULL) {
        return IOT_INVALID_PARAM;
    }
    if ((uint32_t)alarm >= IOT_ARRAY_SIZE(s_rtc_alarm)) {
        return IOT_INVALID_PARAM;
    }
    ctx = &s_rtc_alarm[alarm];
    ctx->enabled = true;
    ctx->fired = false;
    ctx->time = config->time;
    ctx->callback = config->callback;
    ctx->user_data = config->user_data;
    return IOT_OK;
}

iot_status_t iot_rtc_disable_alarm(iot_rtc_alarm_t alarm)
{
    if ((uint32_t)alarm >= IOT_ARRAY_SIZE(s_rtc_alarm)) {
        return IOT_INVALID_PARAM;
    }
    s_rtc_alarm[alarm].enabled = false;
    return IOT_OK;
}

iot_status_t iot_rtc_get_timestamp(uint32_t *timestamp)
{
    if (timestamp == NULL) {
        return IOT_INVALID_PARAM;
    }
    *timestamp = pc_rtc_timestamp(&s_rtc_time);
    return IOT_OK;
}

/* ============================== FLASH ============================== */

static void pc_flash_ensure(void)
{
    if (!s_flash_ready) {
        memset(s_flash, 0xFF, sizeof(s_flash));
        s_flash_ready = true;
    }
}

iot_status_t iot_flash_init(iot_flash_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    pc_flash_ensure();
    handle->info.start = 0U;
    handle->info.size = PC_FLASH_SIZE;
    handle->info.erase_size = PC_FLASH_ERASE;
    handle->info.program_size = 1U;
    handle->info.flags = IOT_FLASH_FLAG_READ | IOT_FLASH_FLAG_WRITE | IOT_FLASH_FLAG_ERASE;
    pc_handle_ready(&handle->base, IOT_STATE_READY);
    return IOT_OK;
}

iot_status_t iot_flash_deinit(iot_flash_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    pc_handle_deinit(&handle->base);
    return IOT_OK;
}

iot_status_t iot_flash_get_info(iot_flash_t *handle, iot_flash_info_t *info)
{
    if (handle == NULL || info == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    *info = handle->info;
    return IOT_OK;
}

iot_status_t iot_flash_read(iot_flash_t *handle, uintptr_t address,
                              void *buffer, size_t length)
{
    if (handle == NULL || (buffer == NULL && length > 0U)) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    if (address > (uintptr_t)PC_FLASH_SIZE ||
        length > (size_t)((uintptr_t)PC_FLASH_SIZE - address)) {
        return IOT_INVALID_PARAM;
    }
    pc_flash_ensure();
    if (length > 0U) {
        memcpy(buffer, &s_flash[address], length);
    }
    return IOT_OK;
}

iot_status_t iot_flash_write(iot_flash_t *handle, uintptr_t address,
                               const void *buffer, size_t length)
{
    if (handle == NULL || (buffer == NULL && length > 0U)) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    if (address > (uintptr_t)PC_FLASH_SIZE ||
        length > (size_t)((uintptr_t)PC_FLASH_SIZE - address)) {
        return IOT_INVALID_PARAM;
    }
    pc_flash_ensure();
    if (length > 0U) {
        memcpy(&s_flash[address], buffer, length);
    }
    return IOT_OK;
}

iot_status_t iot_flash_erase(iot_flash_t *handle, uintptr_t address, size_t length)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    if (address > (uintptr_t)PC_FLASH_SIZE ||
        length > (size_t)((uintptr_t)PC_FLASH_SIZE - address)) {
        return IOT_INVALID_PARAM;
    }
    pc_flash_ensure();
    if (length > 0U) {
        memset(&s_flash[address], 0xFF, length);
    }
    return IOT_OK;
}

iot_status_t iot_flash_lock(iot_flash_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    return IOT_OK;
}

iot_status_t iot_flash_unlock(iot_flash_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    return IOT_OK;
}

/* ============================== CRC ============================== */

static pc_crc_ctx_t *pc_crc_ctx(iot_crc_t *handle, bool create)
{
    int index;

    if (handle == NULL) {
        return NULL;
    }
    for (index = 0; index < PC_CRC_MAX; index++) {
        if (s_crc[index].used && s_crc[index].owner == handle) {
            return &s_crc[index];
        }
    }
    if (!create) {
        return NULL;
    }
    for (index = 0; index < PC_CRC_MAX; index++) {
        if (!s_crc[index].used) {
            s_crc[index].used = true;
            s_crc[index].owner = handle;
            s_crc[index].state = 0U;
            return &s_crc[index];
        }
    }
    return NULL;
}

static uint32_t pc_crc_reverse_bits(uint32_t value, uint32_t width)
{
    uint32_t result = 0U;
    uint32_t i;

    for (i = 0U; i < width; i++) {
        result = (result << 1) | (value & 1U);
        value >>= 1;
    }
    return result;
}

static uint32_t pc_crc_compute(const iot_crc_config_t *config,
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
            byte = pc_crc_reverse_bits(byte, 8U);
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
        crc = pc_crc_reverse_bits(crc, width);
    }
    return (crc ^ config->xor_output) & mask;
}

iot_status_t iot_crc_init(iot_crc_t *handle, const iot_crc_config_t *config)
{
    pc_crc_ctx_t *ctx;

    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (config->width != IOT_CRC_WIDTH_8 &&
        config->width != IOT_CRC_WIDTH_16 &&
        config->width != IOT_CRC_WIDTH_32) {
        return IOT_INVALID_PARAM;
    }
    ctx = pc_crc_ctx(handle, true);
    if (ctx == NULL) {
        return IOT_NO_MEMORY;
    }
    handle->config = *config;
    ctx->state = config->initial_value;
    pc_handle_ready(&handle->base, IOT_STATE_READY);
    return IOT_OK;
}

iot_status_t iot_crc_reset(iot_crc_t *handle)
{
    pc_crc_ctx_t *ctx;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    ctx = pc_crc_ctx(handle, false);
    if (ctx == NULL) {
        return IOT_NOT_READY;
    }
    ctx->state = handle->config.initial_value;
    return IOT_OK;
}

iot_status_t iot_crc_calculate(iot_crc_t *handle, const void *data,
                                 size_t length, uint32_t *result)
{
    pc_crc_ctx_t *ctx;

    if (handle == NULL || result == NULL || (data == NULL && length > 0U)) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    ctx = pc_crc_ctx(handle, false);
    if (ctx == NULL) {
        return IOT_NOT_READY;
    }
    ctx->state = pc_crc_compute(&handle->config, (const uint8_t *)data, length);
    *result = ctx->state;
    return IOT_OK;
}

iot_status_t iot_crc_deinit(iot_crc_t *handle)
{
    int index;

    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    for (index = 0; index < PC_CRC_MAX; index++) {
        if (s_crc[index].used && s_crc[index].owner == handle) {
            memset(&s_crc[index], 0, sizeof(s_crc[index]));
            break;
        }
    }
    pc_handle_deinit(&handle->base);
    return IOT_OK;
}

/* ============================== RNG ============================== */

static uint32_t pc_rng_next(void)
{
    s_rng_state = s_rng_state * 1664525U + 1013904223U;
    return s_rng_state;
}

iot_status_t iot_rng_init(iot_rng_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    s_rng_state = PC_RNG_SEED;
    pc_handle_ready(&handle->base, IOT_STATE_READY);
    return IOT_OK;
}

iot_status_t iot_rng_deinit(iot_rng_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    pc_handle_deinit(&handle->base);
    return IOT_OK;
}

iot_status_t iot_rng_get(iot_rng_t *handle, uint32_t *value)
{
    if (handle == NULL || value == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    *value = pc_rng_next();
    return IOT_OK;
}

iot_status_t iot_rng_generate(iot_rng_t *handle, void *buffer, size_t length)
{
    uint8_t *out = (uint8_t *)buffer;
    size_t i;

    if (handle == NULL || (buffer == NULL && length > 0U)) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    for (i = 0U; i < length; i++) {
        if ((i % 4U) == 0U) {
            uint32_t value = pc_rng_next();
            size_t j;
            for (j = 0U; j < 4U && (i + j) < length; j++) {
                out[i + j] = (uint8_t)(value >> (8U * j));
            }
        }
    }
    return IOT_OK;
}

/* ============================== PWR ============================== */

iot_status_t iot_pwr_init(void)
{
    return IOT_OK;
}

iot_status_t iot_pwr_enter(const iot_pwr_config_t *config)
{
    if (config == NULL) {
        return IOT_INVALID_PARAM;
    }
    s_pwr_status.wakeup_flags = config->wakeup_sources;
    IOT_UNUSED(config->low_power_regulator);
    return IOT_OK;
}

iot_status_t iot_pwr_get_status(iot_pwr_status_t *status)
{
    if (status == NULL) {
        return IOT_INVALID_PARAM;
    }
    *status = s_pwr_status;
    return IOT_OK;
}

iot_status_t iot_pwr_clear_flags(void)
{
    s_pwr_status.reset_flags = 0U;
    s_pwr_status.wakeup_flags = 0U;
    return IOT_OK;
}

/* ============================== QSPI ============================== */

iot_status_t iot_qspi_init(iot_qspi_t *handle, const iot_qspi_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    pc_handle_ready(&handle->base, IOT_STATE_READY);
    return IOT_OK;
}

iot_status_t iot_qspi_deinit(iot_qspi_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    pc_handle_deinit(&handle->base);
    return IOT_OK;
}

iot_status_t iot_qspi_command(iot_qspi_t *handle, const iot_qspi_command_t *command)
{
    if (handle == NULL || command == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    return IOT_OK;
}

iot_status_t iot_qspi_transmit(iot_qspi_t *handle, const void *data,
                                 size_t length, uint32_t timeout)
{
    if (handle == NULL || (data == NULL && length > 0U)) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    IOT_UNUSED(timeout);
    return IOT_OK;
}

iot_status_t iot_qspi_receive(iot_qspi_t *handle, void *data,
                                size_t length, uint32_t timeout)
{
    if (handle == NULL || (data == NULL && length > 0U)) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    IOT_UNUSED(timeout);
    if (length > 0U) {
        memset(data, 0, length);
    }
    return IOT_OK;
}

iot_status_t iot_qspi_memory_map(iot_qspi_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    s_qspi_mapped = true;
    return IOT_OK;
}

iot_status_t iot_qspi_memory_unmap(iot_qspi_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    s_qspi_mapped = false;
    return IOT_OK;
}

/* ============================== SDIO ============================== */

static bool pc_sdio_range_valid(uint32_t block, size_t block_count)
{
    if (block > PC_SDIO_BLOCKS) {
        return false;
    }
    return block_count <= (size_t)(PC_SDIO_BLOCKS - block);
}

iot_status_t iot_sdio_init(iot_sdio_t *handle, const iot_sdio_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    pc_handle_ready(&handle->base, IOT_STATE_READY);
    return IOT_OK;
}

iot_status_t iot_sdio_deinit(iot_sdio_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    pc_handle_deinit(&handle->base);
    return IOT_OK;
}

iot_status_t iot_sdio_read(iot_sdio_t *handle, uint32_t block, void *buffer,
                             size_t block_count, uint32_t timeout)
{
    size_t offset;

    if (handle == NULL || (buffer == NULL && block_count > 0U)) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    if (!pc_sdio_range_valid(block, block_count)) {
        return IOT_INVALID_PARAM;
    }
    IOT_UNUSED(timeout);
    offset = (size_t)block * PC_SDIO_BLOCK_SIZE;
    if (block_count > 0U) {
        memcpy(buffer, &s_sdio[offset], block_count * PC_SDIO_BLOCK_SIZE);
    }
    return IOT_OK;
}

iot_status_t iot_sdio_write(iot_sdio_t *handle, uint32_t block,
                              const void *buffer, size_t block_count, uint32_t timeout)
{
    size_t offset;

    if (handle == NULL || (buffer == NULL && block_count > 0U)) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    if (!pc_sdio_range_valid(block, block_count)) {
        return IOT_INVALID_PARAM;
    }
    IOT_UNUSED(timeout);
    offset = (size_t)block * PC_SDIO_BLOCK_SIZE;
    if (block_count > 0U) {
        memcpy(&s_sdio[offset], buffer, block_count * PC_SDIO_BLOCK_SIZE);
    }
    return IOT_OK;
}

iot_status_t iot_sdio_read_dma(iot_sdio_t *handle, uint32_t block,
                                 void *buffer, size_t block_count)
{
    return iot_sdio_read(handle, block, buffer, block_count, IOT_TIMEOUT_FOREVER);
}

iot_status_t iot_sdio_write_dma(iot_sdio_t *handle, uint32_t block,
                                  const void *buffer, size_t block_count)
{
    return iot_sdio_write(handle, block, buffer, block_count, IOT_TIMEOUT_FOREVER);
}

/* ============================== USB ============================== */

iot_status_t iot_usb_init(iot_usb_t *handle, const iot_usb_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    s_usb_started = false;
    s_usb_connected = false;
    pc_handle_ready(&handle->base, IOT_STATE_READY);
    return IOT_OK;
}

iot_status_t iot_usb_deinit(iot_usb_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    s_usb_started = false;
    s_usb_connected = false;
    pc_handle_deinit(&handle->base);
    return IOT_OK;
}

iot_status_t iot_usb_start(iot_usb_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    s_usb_started = true;
    return IOT_OK;
}

iot_status_t iot_usb_stop(iot_usb_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    s_usb_started = false;
    s_usb_connected = false;
    return IOT_OK;
}

iot_status_t iot_usb_connect(iot_usb_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    s_usb_connected = true;
    return IOT_OK;
}

iot_status_t iot_usb_disconnect(iot_usb_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    s_usb_connected = false;
    return IOT_OK;
}

/* ============================== ETH ============================== */

iot_status_t iot_eth_init(iot_eth_t *handle, const iot_eth_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return IOT_INVALID_PARAM;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    s_eth_started = false;
    s_eth_len = 0U;
    pc_handle_ready(&handle->base, IOT_STATE_READY);
    return IOT_OK;
}

iot_status_t iot_eth_deinit(iot_eth_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    s_eth_started = false;
    s_eth_len = 0U;
    pc_handle_deinit(&handle->base);
    return IOT_OK;
}

iot_status_t iot_eth_start(iot_eth_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    s_eth_started = true;
    return IOT_OK;
}

iot_status_t iot_eth_stop(iot_eth_t *handle)
{
    if (handle == NULL) {
        return IOT_INVALID_PARAM;
    }
    s_eth_started = false;
    return IOT_OK;
}

iot_status_t iot_eth_transmit(iot_eth_t *handle, const void *data, size_t length)
{
    if (handle == NULL || (data == NULL && length > 0U)) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET || !s_eth_started) {
        return IOT_NOT_READY;
    }
    s_eth_len = IOT_MIN(length, PC_ETH_FRAME_MAX);
    if (s_eth_len > 0U) {
        memcpy(s_eth_frame, data, s_eth_len);
    }
    return IOT_OK;
}

iot_status_t iot_eth_receive(iot_eth_t *handle, void *buffer,
                               size_t buffer_size, size_t *received_length)
{
    if (handle == NULL || buffer == NULL || received_length == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (handle->base.state == IOT_STATE_RESET) {
        return IOT_NOT_READY;
    }
    IOT_UNUSED(buffer_size);
    *received_length = 0U;
    return IOT_OK;
}

/* ============================== 测试辅助 ============================== */

void iot_pc_reset_state(void)
{
    iot_pc_os_reset_state();

    memset(s_clock_freq, 0, sizeof(s_clock_freq));
    memset(s_gpio, 0, sizeof(s_gpio));
    memset(s_exti, 0, sizeof(s_exti));

    pc_uart_reset();
    memset(s_i2c_mem, 0, sizeof(s_i2c_mem));
    s_i2c_reg = 0U;

    memset(s_i2s_buf, 0, sizeof(s_i2s_buf));
    s_i2s_len = 0U;

    memset(s_can, 0, sizeof(s_can));

    memset(s_adc_value, 0, sizeof(s_adc_value));
    memset(s_adc_max, 0, sizeof(s_adc_max));
    memset(s_dac_value, 0, sizeof(s_dac_value));

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

    s_rng_state = PC_RNG_SEED;

    memset(&s_pwr_status, 0, sizeof(s_pwr_status));

    s_qspi_mapped = false;

    memset(s_sdio, 0, sizeof(s_sdio));

    s_usb_started = false;
    s_usb_connected = false;

    s_eth_started = false;
    s_eth_len = 0U;
    memset(s_eth_frame, 0, sizeof(s_eth_frame));

}


void iot_pc_adc_set(uint8_t channel, uint32_t value)
{
    if (channel < PC_ADC_CHANNELS) {
        s_adc_value[channel] = value;
    }
}

void iot_pc_adc_set_max(uint8_t channel, uint32_t max)
{
    if (channel < PC_ADC_CHANNELS) {
        s_adc_max[channel] = max;
    }
}

void iot_pc_exti_trigger(iot_pin_t pin)
{
    pc_exti_t *exti;

    if (!pc_gpio_valid(pin)) {
        return;
    }
    exti = &s_exti[IOT_PIN_GET_PORT(pin)][IOT_PIN_GET_INDEX(pin)];
    if (exti->initialized && exti->enabled && exti->config.callback != NULL) {
        exti->config.callback(pin, exti->config.user_data);
    }
}

void iot_pc_can_inject(iot_can_t *h, const iot_can_frame_t *frame)
{
    pc_can_ctx_t *ctx;
    uint32_t tail;

    if (h == NULL || frame == NULL) {
        return;
    }
    ctx = pc_can_ctx(h, true);
    if (ctx == NULL) {
        return;
    }
    if (ctx->count >= PC_CAN_QUEUE) {
        ctx->head = (ctx->head + 1U) % PC_CAN_QUEUE;
        ctx->count--;
    }
    tail = (ctx->head + ctx->count) % PC_CAN_QUEUE;
    ctx->queue[tail] = *frame;
    ctx->count++;
    if (h->base.state != IOT_STATE_RESET && h->callback != NULL) {
        h->callback(h, IOT_EVENT_RX_COMPLETE, h->user_data);
    }
}

void iot_pc_capture_set(iot_capture_t *h, uint32_t value)
{
    pc_capture_ctx_t *ctx;

    if (h == NULL) {
        return;
    }
    ctx = pc_capture_ctx(h, true);
    if (ctx != NULL) {
        ctx->value = value;
    }
}

void iot_pc_encoder_set(iot_encoder_t *h, int32_t count)
{
    pc_encoder_ctx_t *ctx;

    if (h == NULL) {
        return;
    }
    ctx = pc_encoder_ctx(h, true);
    if (ctx != NULL) {
        ctx->count = count;
    }
}

void iot_pc_i2c_set_mem(uint16_t addr, uint8_t reg, uint8_t value)
{
    if (addr <= 0xFFU) {
        s_i2c_mem[addr][reg] = value;
    }
}

void iot_pc_time_set(const iot_rtc_time_t *t)
{
    if (t != NULL) {
        s_rtc_time = *t;
    }
}

uint32_t iot_pc_wdg_refresh_count(void)
{
    return s_wdg_refresh_count;
}

/* ============================== 测试钩子转发 ============================== */
/*
 * 测试统一包含 pc.h 中的 iot_test_* 声明，
 * 由本文件把调用转发到 iot_pc_* 内部接口。
 */

void iot_test_reset_state(void)
{
    iot_pc_reset_state();
}

void iot_test_tick_advance(uint32_t ms)
{
    iot_pc_tick_advance(ms);
}

uint32_t iot_test_uart_get_tx(iot_uart_t *h, uint8_t *buf, uint32_t cap)
{
    return iot_pc_uart_get_tx(h, buf, cap);
}

uint32_t iot_test_uart_inject(iot_uart_t *h,
                               const uint8_t *data, uint32_t len)
{
    return iot_pc_uart_inject(h, data, len);
}

void iot_test_adc_set(uint8_t channel, uint32_t value)
{
    iot_pc_adc_set(channel, value);
}

void iot_test_adc_set_max(uint8_t channel, uint32_t max)
{
    iot_pc_adc_set_max(channel, max);
}

void iot_test_exti_trigger(iot_pin_t pin)
{
    iot_pc_exti_trigger(pin);
}

void iot_test_can_inject(iot_can_t *h, const iot_can_frame_t *frame)
{
    iot_pc_can_inject(h, frame);
}

void iot_test_capture_set(iot_capture_t *h, uint32_t value)
{
    iot_pc_capture_set(h, value);
}

void iot_test_encoder_set(iot_encoder_t *h, int32_t count)
{
    iot_pc_encoder_set(h, count);
}

void iot_test_i2c_set_mem(uint16_t addr, uint8_t reg, uint8_t value)
{
    iot_pc_i2c_set_mem(addr, reg, value);
}

void iot_test_time_set(const iot_rtc_time_t *t)
{
    iot_pc_time_set(t);
}

uint32_t iot_test_wdg_refresh_count(void)
{
    return iot_pc_wdg_refresh_count();
}

bool iot_test_reset_called(void)
{
    return iot_pc_reset_called();
}
