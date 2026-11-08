#include "nrf24l01.h"
#include "nrf24l01_register.h"
#include "log.h"
#define NRF_REG_MASK   0x1FU
#define NRF_IRQ_CLEAR  0x70U
#define NRF_CHECK(h) \
    do { if ((h) == NULL || !(h)->initialized) { return (int)HAL_INVALID_PARAM; } } while (0)

static int nrf24l01_write_reg(nrf24l01_handle_t *h, uint8_t reg,
                              const uint8_t *data, size_t len)
{
    uint8_t cmd = (uint8_t)(NRF24L01_CMD_W_REGISTER | (reg & NRF_REG_MASK));
    int ret;
    driver_spi_select(&h->spi);
    ret = driver_spi_write(&h->spi, &cmd, 1U);
    if (ret == DRIVER_OK) {
        ret = driver_spi_write(&h->spi, data, len);
    }
    driver_spi_deselect(&h->spi);
    return (ret == DRIVER_OK) ? DRIVER_OK : (int)HAL_IO_ERROR;
}

static int nrf24l01_write_reg8(nrf24l01_handle_t *h, uint8_t reg, uint8_t value)
{
    return nrf24l01_write_reg(h, reg, &value, 1U);
}

static int nrf24l01_read_reg(nrf24l01_handle_t *h, uint8_t reg, uint8_t *data, size_t len)
{
    uint8_t cmd = (uint8_t)(NRF24L01_CMD_R_REGISTER | (reg & NRF_REG_MASK));
    driver_spi_select(&h->spi);
    if (driver_spi_write(&h->spi, &cmd, 1U) != DRIVER_OK ||
        driver_spi_read(&h->spi, data, len) != DRIVER_OK) {
        driver_spi_deselect(&h->spi);
        return (int)HAL_IO_ERROR;
    }
    driver_spi_deselect(&h->spi);
    return DRIVER_OK;
}

static int nrf24l01_cmd(nrf24l01_handle_t *h, uint8_t cmd, uint8_t *status)
{
    uint8_t rx = 0U;
    driver_spi_select(&h->spi);
    if (driver_spi_transfer(&h->spi, &cmd, &rx, 1U) != DRIVER_OK) {
        driver_spi_deselect(&h->spi);
        LOG_E("nrf24l01", "spi cmd 0x%02X failed", cmd);
        return (int)HAL_IO_ERROR;
    }
    driver_spi_deselect(&h->spi);
    if (status != NULL) {
        *status = rx;
    }
    return DRIVER_OK;
}

int nrf24l01_set_mode(nrf24l01_handle_t *handle, nrf24l01_mode_t mode)
{
    uint8_t config = 0U;
    NRF_CHECK(handle);
    if (nrf24l01_read_reg(handle, NRF24L01_REG_CONFIG, &config, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (mode == NRF24L01_MODE_POWER_DOWN) {
        config = (uint8_t)(config & ~NRF24L01_CONFIG_PWR_UP);
        (void)driver_gpio_write(handle->ce, HAL_GPIO_LOW);
    } else if (mode == NRF24L01_MODE_RX) {
        config |= (uint8_t)(NRF24L01_CONFIG_PWR_UP | NRF24L01_CONFIG_PRIM_RX);
    } else if (mode == NRF24L01_MODE_STANDBY || mode == NRF24L01_MODE_TX) {
        config = (uint8_t)((config & ~NRF24L01_CONFIG_PRIM_RX) | NRF24L01_CONFIG_PWR_UP);
        (void)driver_gpio_write(handle->ce, HAL_GPIO_LOW);
    } else {
        return (int)HAL_INVALID_PARAM;
    }
    if (nrf24l01_write_reg8(handle, NRF24L01_REG_CONFIG, config) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (mode == NRF24L01_MODE_RX) {
        driver_delay_us(NRF24L01_RX_SETTLE_US);
        (void)driver_gpio_write(handle->ce, HAL_GPIO_HIGH);
    }
    return DRIVER_OK;
}

int nrf24l01_set_frequency(nrf24l01_handle_t *handle, uint32_t frequency_khz)
{
    uint8_t channel;
    NRF_CHECK(handle);
    if (frequency_khz < NRF24L01_FREQ_BASE_KHZ || frequency_khz > NRF24L01_FREQ_MAX_KHZ) {
        return (int)HAL_INVALID_PARAM;
    }
    channel = (uint8_t)((frequency_khz - NRF24L01_FREQ_BASE_KHZ) / NRF24L01_FREQ_STEP_KHZ);
    if (channel > NRF24L01_CHANNEL_MAX ||
        nrf24l01_write_reg8(handle, NRF24L01_REG_RF_CH, channel) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    handle->channel = channel;
    return DRIVER_OK;
}

int nrf24l01_set_address(nrf24l01_handle_t *handle, const uint8_t *address, uint8_t width)
{
    NRF_CHECK(handle);
    if (address == NULL || width < NRF24L01_ADDR_WIDTH_MIN || width > NRF24L01_ADDR_WIDTH_MAX) {
        return (int)HAL_INVALID_PARAM;
    }
    if (nrf24l01_write_reg8(handle, NRF24L01_REG_SETUP_AW, (uint8_t)(width - 2U)) != DRIVER_OK ||
        nrf24l01_write_reg(handle, NRF24L01_REG_TX_ADDR, address, width) != DRIVER_OK ||
        nrf24l01_write_reg(handle, NRF24L01_REG_RX_ADDR_P0, address, width) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    handle->address_width = width;
    return DRIVER_OK;
}

int nrf24l01_send(nrf24l01_handle_t *handle, const uint8_t *data, size_t len)
{
    uint8_t cmd = NRF24L01_CMD_W_TX_PAYLOAD;
    uint8_t status = 0U;
    uint32_t start;
    int ret;

    NRF_CHECK(handle);
    if (data == NULL || len == 0U || len > handle->payload_size) {
        return (int)HAL_INVALID_PARAM;
    }
    if (nrf24l01_set_mode(handle, NRF24L01_MODE_STANDBY) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    (void)nrf24l01_cmd(handle, NRF24L01_CMD_FLUSH_TX, NULL);
    (void)nrf24l01_write_reg8(handle, NRF24L01_REG_STATUS, NRF_IRQ_CLEAR);
    driver_spi_select(&handle->spi);
    ret = driver_spi_write(&handle->spi, &cmd, 1U);
    if (ret == DRIVER_OK) {
        ret = driver_spi_write(&handle->spi, data, len);
    }
    driver_spi_deselect(&handle->spi);
    if (ret != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    /* CE 高电平脉冲触发发射 */
    (void)driver_gpio_write(handle->ce, HAL_GPIO_HIGH);
    driver_delay_us(NRF24L01_CE_PULSE_US);
    (void)driver_gpio_write(handle->ce, HAL_GPIO_LOW);
    start = driver_tick_ms();
    for (;;) {
        if (nrf24l01_cmd(handle, NRF24L01_CMD_NOP, &status) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
        if ((status & NRF24L01_STATUS_TX_DS) != 0U) {
            (void)nrf24l01_write_reg8(handle, NRF24L01_REG_STATUS, NRF24L01_STATUS_TX_DS);
            return DRIVER_OK;
        }
        if ((status & NRF24L01_STATUS_MAX_RT) != 0U) {
            (void)nrf24l01_write_reg8(handle, NRF24L01_REG_STATUS, NRF24L01_STATUS_MAX_RT);
            (void)nrf24l01_cmd(handle, NRF24L01_CMD_FLUSH_TX, NULL);
            return (int)HAL_IO_ERROR;
        }
        if ((uint32_t)(driver_tick_ms() - start) > handle->timeout_ms) {
            (void)nrf24l01_cmd(handle, NRF24L01_CMD_FLUSH_TX, NULL);
            return (int)HAL_TIMEOUT;
        }
        driver_delay_us(50U);
    }
}

int nrf24l01_poll(nrf24l01_handle_t *handle, uint8_t *data, size_t len, size_t *received)
{
    uint8_t status = 0U;
    uint8_t cmd = NRF24L01_CMD_R_RX_PAYLOAD;
    size_t width;

    NRF_CHECK(handle);
    if (data == NULL || len == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    if (nrf24l01_cmd(handle, NRF24L01_CMD_NOP, &status) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if ((status & NRF24L01_STATUS_RX_DR) == 0U ||
        ((status & NRF24L01_STATUS_RX_P_NO) >> 1) == NRF24L01_RX_PIPE_EMPTY) {
        return (int)HAL_NOT_READY;
    }
    width = handle->payload_size;
    if (len < width) {
        (void)nrf24l01_cmd(handle, NRF24L01_CMD_FLUSH_RX, NULL);
        (void)nrf24l01_write_reg8(handle, NRF24L01_REG_STATUS, NRF_IRQ_CLEAR);
        return (int)HAL_OVERFLOW;
    }
    driver_spi_select(&handle->spi);
    if (driver_spi_write(&handle->spi, &cmd, 1U) != DRIVER_OK ||
        driver_spi_read(&handle->spi, data, width) != DRIVER_OK) {
        driver_spi_deselect(&handle->spi);
        return (int)HAL_IO_ERROR;
    }
    driver_spi_deselect(&handle->spi);
    (void)nrf24l01_write_reg8(handle, NRF24L01_REG_STATUS, NRF24L01_STATUS_RX_DR);
    if (received != NULL) {
        *received = width;
    }
    return DRIVER_OK;
}

int nrf24l01_recv(nrf24l01_handle_t *handle, uint8_t *data, size_t len,
                  size_t *received, uint32_t timeout_ms)
{
    uint32_t start;
    int ret;

    NRF_CHECK(handle);
    if (data == NULL || len == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    if (nrf24l01_set_mode(handle, NRF24L01_MODE_RX) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    start = driver_tick_ms();
    for (;;) {
        ret = nrf24l01_poll(handle, data, len, received);
        if (ret != (int)HAL_NOT_READY) {
            return ret;
        }
        if ((uint32_t)(driver_tick_ms() - start) > timeout_ms) {
            return (int)HAL_TIMEOUT;
        }
        driver_delay_us(100U);
    }
}

int nrf24l01_init(nrf24l01_handle_t *handle, hal_spi_t *spi,
                  hal_pin_t cs, hal_pin_t ce, hal_pin_t irq)
{
    static const uint8_t defaults[][2] = {
        { NRF24L01_REG_EN_AA, 0x01U },
        { NRF24L01_REG_EN_RXADDR, 0x01U },
        { NRF24L01_REG_SETUP_RETR, 0x1AU },
        { NRF24L01_REG_RF_SETUP, NRF24L01_RF_SETUP_PWR_0DBM },
        { NRF24L01_REG_RX_PW_P0, (uint8_t)NRF24L01_PAYLOAD_MAX },
    };
    uint8_t value = 0U;
    size_t i;

    if (handle == NULL || spi == NULL || ce == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->spi.handle = spi;
    handle->spi.cs = cs;
    handle->spi.timeout_ms = DRIVER_SPI_TIMEOUT_DEFAULT;
    handle->ce = ce;
    handle->irq = irq;
    handle->channel = 76U;
    handle->payload_size = (uint8_t)NRF24L01_PAYLOAD_MAX;
    handle->address_width = (uint8_t)NRF24L01_ADDR_WIDTH_MAX;
    handle->timeout_ms = 100U;
    handle->initialized = false;
    if ((cs != 0U && driver_gpio_output(cs, HAL_GPIO_HIGH) != DRIVER_OK) ||
        driver_gpio_output(ce, HAL_GPIO_LOW) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (irq != 0U && driver_gpio_input(irq, HAL_GPIO_PULLUP) != DRIVER_OK) {
        LOG_W("nrf24l01", "irq pin init failed");
    }
    driver_delay_ms(100U);   /* Tpor */
    /* 器件探测：SETUP_AW 仅 bit1:0 有效，回读必须等于写入值 */
    if (nrf24l01_write_reg8(handle, NRF24L01_REG_SETUP_AW, 0x03U) != DRIVER_OK ||
        nrf24l01_read_reg(handle, NRF24L01_REG_SETUP_AW, &value, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if ((value & 0x03U) != 0x03U) {
        LOG_E("nrf24l01", "not found (SETUP_AW=0x%02X)", value);
        return (int)HAL_NOT_FOUND;
    }
    for (i = 0U; i < DRIVER_ARRAY_SIZE(defaults); i++) {
        (void)nrf24l01_write_reg8(handle, defaults[i][0], defaults[i][1]);
    }
    (void)nrf24l01_write_reg8(handle, NRF24L01_REG_RF_CH, handle->channel);
    (void)nrf24l01_write_reg8(handle, NRF24L01_REG_STATUS, NRF_IRQ_CLEAR);
    (void)nrf24l01_cmd(handle, NRF24L01_CMD_FLUSH_TX, NULL);
    (void)nrf24l01_cmd(handle, NRF24L01_CMD_FLUSH_RX, NULL);
    handle->initialized = true;
    (void)nrf24l01_write_reg8(handle, NRF24L01_REG_CONFIG,
                              (uint8_t)(NRF24L01_CONFIG_EN_CRC | NRF24L01_CONFIG_CRCO));
    return DRIVER_OK;
}

int nrf24l01_deinit(nrf24l01_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)nrf24l01_set_mode(handle, NRF24L01_MODE_POWER_DOWN);
    }
    if (handle->ce != 0U) {
        (void)hal_gpio_deinit(handle->ce);
    }
    handle->initialized = false;
    return DRIVER_OK;
}
