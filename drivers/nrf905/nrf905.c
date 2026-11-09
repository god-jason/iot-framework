#include <string.h>
#include "nrf905.h"
#include "nrf905_register.h"
#include "log.h"
#define NRF905_CHECK(h) \
    do { if ((h) == NULL || !(h)->initialized) { return (int)HAL_INVALID_PARAM; } } while (0)

static int nrf905_write_cmd(nrf905_handle_t *h, uint8_t cmd,
                            const uint8_t *data, size_t len)
{
    driver_spi_select(&h->spi);
    if (driver_spi_write(&h->spi, &cmd, 1U) != DRIVER_OK ||
        (len > 0U && driver_spi_write(&h->spi, data, len) != DRIVER_OK)) {
        driver_spi_deselect(&h->spi);
        return (int)HAL_IO_ERROR;
    }
    driver_spi_deselect(&h->spi);
    return DRIVER_OK;
}

static int nrf905_read_cmd(nrf905_handle_t *h, uint8_t cmd,
                           uint8_t *data, size_t len)
{
    driver_spi_select(&h->spi);
    if (driver_spi_write(&h->spi, &cmd, 1U) != DRIVER_OK ||
        driver_spi_read(&h->spi, data, len) != DRIVER_OK) {
        driver_spi_deselect(&h->spi);
        return (int)HAL_IO_ERROR;
    }
    driver_spi_deselect(&h->spi);
    return DRIVER_OK;
}
/* 读配置命令的第 1 个返回字节即为 STATUS */
static int nrf905_status(nrf905_handle_t *h, uint8_t *status)
{
    return nrf905_read_cmd(h, NRF905_CMD_R_CONFIG, status, 1U);
}

static int nrf905_dr_ready(nrf905_handle_t *h, uint8_t *ready)
{
    uint8_t status = 0U;
    if (h->dr != 0U) {
        int level = driver_gpio_read(h->dr);
        if (level < 0) {
            return (int)HAL_IO_ERROR;
        }
        *ready = (level == 1) ? 1U : 0U;
        return DRIVER_OK;
    }
    if (nrf905_status(h, &status) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    *ready = ((status & NRF905_STATUS_DR) != 0U) ? 1U : 0U;
    return DRIVER_OK;
}

static int nrf905_wait_ready(nrf905_handle_t *h, uint32_t timeout_ms)
{
    uint32_t start = driver_tick_ms();
    uint8_t ready = 0U;
    for (;;) {
        if (nrf905_dr_ready(h, &ready) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
        if (ready != 0U) {
            return DRIVER_OK;
        }
        if ((uint32_t)(driver_tick_ms() - start) > timeout_ms) {
            return (int)HAL_TIMEOUT;
        }
        driver_delay_us(200U);
    }
}

int nrf905_set_mode(nrf905_handle_t *h, nrf905_mode_e mode)
{
    NRF905_CHECK(h);
    switch (mode) {
    case NRF905_MODE_POWER_DOWN:
        (void)driver_gpio_write(h->pwr_up, HAL_GPIO_LOW);
        (void)driver_gpio_write(h->trx_ce, HAL_GPIO_LOW);
        (void)driver_gpio_write(h->tx_en, HAL_GPIO_LOW);
        break;
    case NRF905_MODE_STANDBY:
        (void)driver_gpio_write(h->trx_ce, HAL_GPIO_LOW);
        (void)driver_gpio_write(h->pwr_up, HAL_GPIO_HIGH);
        break;
    case NRF905_MODE_RX:
        (void)driver_gpio_write(h->tx_en, HAL_GPIO_LOW);
        (void)driver_gpio_write(h->pwr_up, HAL_GPIO_HIGH);
        driver_delay_us(NRF905_RX_SETTLE_US);
        (void)driver_gpio_write(h->trx_ce, HAL_GPIO_HIGH);
        break;
    case NRF905_MODE_TX:
        (void)driver_gpio_write(h->tx_en, HAL_GPIO_HIGH);
        (void)driver_gpio_write(h->pwr_up, HAL_GPIO_HIGH);
        driver_delay_us(NRF905_TX_SETTLE_US);
        (void)driver_gpio_write(h->trx_ce, HAL_GPIO_HIGH);
        break;
    default:
        return (int)HAL_INVALID_PARAM;
    }
    return DRIVER_OK;
}

int nrf905_set_frequency(nrf905_handle_t *h, uint32_t frequency_khz)
{
    uint8_t channel;
    uint8_t rf_config;
    NRF905_CHECK(h);
    rf_config = (uint8_t)(h->config[NRF905_CFG_RF_CONFIG] &
                          (uint8_t)~(NRF905_RF_CONFIG_CH8 | NRF905_RF_CONFIG_HFREQ));
    if (frequency_khz >= NRF905_FREQ_LOW_MIN_KHZ && frequency_khz <= NRF905_FREQ_LOW_MAX_KHZ) {
        channel = (uint8_t)((frequency_khz - NRF905_FREQ_LOW_MIN_KHZ) / NRF905_FREQ_STEP_KHZ);
    } else if (frequency_khz >= NRF905_FREQ_HIGH_MIN_KHZ &&
               frequency_khz <= NRF905_FREQ_HIGH_MAX_KHZ) {
        channel = (uint8_t)((frequency_khz - NRF905_FREQ_HIGH_MIN_KHZ) / NRF905_FREQ_STEP_KHZ);
        rf_config |= NRF905_RF_CONFIG_HFREQ;
    } else {
        return (int)HAL_INVALID_PARAM;
    }
    if ((channel & NRF905_RF_CONFIG_CH8) != 0U) {
        rf_config |= NRF905_RF_CONFIG_CH8;
    }
    h->config[NRF905_CFG_CH_NO] = channel;
    h->config[NRF905_CFG_RF_CONFIG] = rf_config;
    h->channel = channel;
    h->frequency_khz = frequency_khz;
    return nrf905_write_cmd(h, NRF905_CMD_W_CONFIG, h->config, NRF905_CFG_SIZE);
}

int nrf905_set_address(nrf905_handle_t *h, const uint8_t address[4])
{
    NRF905_CHECK(h);
    if (address == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    (void)memcpy(h->address, address, NRF905_ADDRESS_SIZE);
    return nrf905_write_cmd(h, NRF905_CMD_W_TX_ADDRESS, h->address, NRF905_ADDRESS_SIZE);
}

int nrf905_set_config(nrf905_handle_t *h, const uint8_t config[10])
{
    NRF905_CHECK(h);
    if (config == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    (void)memcpy(h->config, config, NRF905_CFG_SIZE);
    h->channel = h->config[NRF905_CFG_CH_NO];
    return nrf905_write_cmd(h, NRF905_CMD_W_CONFIG, h->config, NRF905_CFG_SIZE);
}

int nrf905_send(nrf905_handle_t *h, const uint8_t *data, size_t len)
{
    uint8_t cmd = NRF905_CMD_W_TX_PAYLOAD;
    uint8_t pad = 0U;
    size_t i;

    NRF905_CHECK(h);
    if (data == NULL || len == 0U || len > NRF905_PAYLOAD_SIZE) {
        return (int)HAL_INVALID_PARAM;
    }
    if (nrf905_set_mode(h, NRF905_MODE_STANDBY) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_spi_select(&h->spi);
    if (driver_spi_write(&h->spi, &cmd, 1U) != DRIVER_OK ||
        driver_spi_write(&h->spi, data, len) != DRIVER_OK) {
        driver_spi_deselect(&h->spi);
        return (int)HAL_IO_ERROR;
    }
    /* ShockBurst 固定 32 字节 payload，不足部分补 0 */
    for (i = len; i < NRF905_PAYLOAD_SIZE; i++) {
        if (driver_spi_write(&h->spi, &pad, 1U) != DRIVER_OK) {
            driver_spi_deselect(&h->spi);
            return (int)HAL_IO_ERROR;
        }
    }
    driver_spi_deselect(&h->spi);
    (void)driver_gpio_write(h->tx_en, HAL_GPIO_HIGH);
    (void)driver_gpio_write(h->trx_ce, HAL_GPIO_HIGH);
    if (nrf905_wait_ready(h, h->timeout_ms) != DRIVER_OK) {
        (void)driver_gpio_write(h->trx_ce, HAL_GPIO_LOW);
        LOG_W("nrf905", "tx timeout");
        return (int)HAL_TIMEOUT;
    }
    (void)driver_gpio_write(h->trx_ce, HAL_GPIO_LOW);
    (void)driver_gpio_write(h->tx_en, HAL_GPIO_LOW);
    return DRIVER_OK;
}

int nrf905_poll(nrf905_handle_t *h, uint8_t *data, size_t len, size_t *received)
{
    uint8_t ready = 0U;
    NRF905_CHECK(h);
    if (data == NULL || len < NRF905_PAYLOAD_SIZE) {
        return (int)HAL_INVALID_PARAM;
    }
    if (nrf905_dr_ready(h, &ready) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (ready == 0U) {
        return (int)HAL_NOT_READY;
    }
    if (nrf905_read_cmd(h, NRF905_CMD_R_RX_PAYLOAD, data, NRF905_PAYLOAD_SIZE) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (received != NULL) {
        *received = NRF905_PAYLOAD_SIZE;
    }
    return DRIVER_OK;
}

int nrf905_recv(nrf905_handle_t *h, uint8_t *data, size_t len,
                size_t *received, uint32_t timeout_ms)
{
    uint32_t start;
    int ret;

    NRF905_CHECK(h);
    if (data == NULL || len < NRF905_PAYLOAD_SIZE) {
        return (int)HAL_INVALID_PARAM;
    }
    if (nrf905_set_mode(h, NRF905_MODE_RX) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    start = driver_tick_ms();
    for (;;) {
        ret = nrf905_poll(h, data, len, received);
        if (ret != (int)HAL_NOT_READY) {
            (void)driver_gpio_write(h->trx_ce, HAL_GPIO_LOW);
            return ret;
        }
        if ((uint32_t)(driver_tick_ms() - start) > timeout_ms) {
            (void)driver_gpio_write(h->trx_ce, HAL_GPIO_LOW);
            return (int)HAL_TIMEOUT;
        }
        driver_delay_us(200U);
    }
}

int nrf905_init(nrf905_handle_t *handle, hal_spi_t *spi, hal_pin_t cs,
                hal_pin_t trx_ce, hal_pin_t tx_en, hal_pin_t pwr_up, hal_pin_t dr)
{
    uint8_t readback[NRF905_CFG_SIZE];

    if (handle == NULL || spi == NULL || trx_ce == 0U || tx_en == 0U || pwr_up == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    (void)memset(handle, 0, sizeof(*handle));
    handle->spi.handle = spi;
    handle->spi.cs = cs;
    handle->spi.timeout_ms = DRIVER_SPI_TIMEOUT_DEFAULT;
    handle->trx_ce = trx_ce;
    handle->tx_en = tx_en;
    handle->pwr_up = pwr_up;
    handle->dr = dr;
    handle->timeout_ms = 100U;
    if ((cs != 0U && driver_gpio_output(cs, HAL_GPIO_HIGH) != DRIVER_OK) ||
        driver_gpio_output(trx_ce, HAL_GPIO_LOW) != DRIVER_OK ||
        driver_gpio_output(tx_en, HAL_GPIO_LOW) != DRIVER_OK ||
        driver_gpio_output(pwr_up, HAL_GPIO_LOW) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (dr != 0U && driver_gpio_input(dr, HAL_GPIO_NOPULL) != DRIVER_OK) { return (int)HAL_IO_ERROR; }
    driver_delay_ms(3U);
    /* 默认地址与通道；AFW/输出功率等位域需按 datasheet 通过 set_config 补充 */
    (void)memset(handle->address, 0xE7, NRF905_ADDRESS_SIZE);
    (void)memset(handle->config, 0, sizeof(handle->config));
    handle->initialized = true;   /* set_frequency 等接口会校验该标记 */
    if (nrf905_set_frequency(handle, 433920U) != DRIVER_OK ||
        nrf905_write_cmd(handle, NRF905_CMD_W_TX_ADDRESS,
                         handle->address, NRF905_ADDRESS_SIZE) != DRIVER_OK) {
        handle->initialized = false;
        return (int)HAL_IO_ERROR;
    }
    /* 无 ID 寄存器：回读配置区比对作为探测 */
    if (nrf905_read_cmd(handle, NRF905_CMD_R_CONFIG, readback, NRF905_CFG_SIZE) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (readback[NRF905_CFG_CH_NO] != handle->config[NRF905_CFG_CH_NO] ||
        readback[NRF905_CFG_RF_CONFIG] != handle->config[NRF905_CFG_RF_CONFIG]) {
        LOG_E("nrf905", "config readback mismatch");
        return (int)HAL_NOT_FOUND;
    }
    return DRIVER_OK;
}

int nrf905_deinit(nrf905_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)nrf905_set_mode(handle, NRF905_MODE_POWER_DOWN);
    }
    handle->initialized = false;
    return DRIVER_OK;
}
