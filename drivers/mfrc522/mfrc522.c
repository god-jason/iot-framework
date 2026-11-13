#include <string.h>
#include "mfrc522.h"
#include "mfrc522_register.h"
#include "ros.h"
#include "common.h"

#define MFRC522_CHECK(h) \
    do { if ((h) == NULL || !(h)->initialized) { return (int)HAL_INVALID_PARAM; } } while (0)

static int mfrc522_reg_read(mfrc522_handle_t *h, uint8_t reg, uint8_t *value)
{
    uint8_t addr = (uint8_t)(((reg << 1) & 0x7EU) | 0x80U);
    hal_spi_dev_select(&h->spi);
    if (hal_spi_dev_write(&h->spi, &addr, 1U) != HAL_OK ||
        hal_spi_dev_read(&h->spi, value, 1U) != HAL_OK) {
        hal_spi_dev_deselect(&h->spi);
        return (int)HAL_IO_ERROR;
    }
    hal_spi_dev_deselect(&h->spi);
    return HAL_OK;
}

static int mfrc522_reg_write(mfrc522_handle_t *h, uint8_t reg, uint8_t value)
{
    uint8_t buf[2];
    buf[0] = (uint8_t)((reg << 1) & 0x7EU);
    buf[1] = value;
    return (hal_spi_dev_write_cs(&h->spi, buf, 2U) == HAL_OK)
               ? HAL_OK : (int)HAL_IO_ERROR;
}

static int mfrc522_set_bits(mfrc522_handle_t *h, uint8_t reg, uint8_t mask)
{
    uint8_t value = 0U;
    if (mfrc522_reg_read(h, reg, &value) != HAL_OK) { return (int)HAL_IO_ERROR; }
    return mfrc522_reg_write(h, reg, (uint8_t)(value | mask));
}

int mfrc522_get_version(mfrc522_handle_t *h, uint8_t *version)
{
    MFRC522_CHECK(h);
    if (version == NULL) { return (int)HAL_INVALID_PARAM; }
    return mfrc522_reg_read(h, MFRC522_REG_VERSION, version);
}

int mfrc522_set_antenna(mfrc522_handle_t *h, bool on)
{
    uint8_t value = 0U;
    MFRC522_CHECK(h);
    if (on) {
        if (mfrc522_reg_read(h, MFRC522_REG_TX_CONTROL, &value) != HAL_OK) {
            return (int)HAL_IO_ERROR;
        }
        if ((value & (MFRC522_TX1_RF_ON | MFRC522_TX2_RF_ON)) == 0U) {
            return mfrc522_set_bits(h, MFRC522_REG_TX_CONTROL,
                                    MFRC522_TX1_RF_ON | MFRC522_TX2_RF_ON);
        }
        return HAL_OK;
    }
    if (mfrc522_reg_read(h, MFRC522_REG_TX_CONTROL, &value) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }
    value = (uint8_t)(value & (uint8_t)~(MFRC522_TX1_RF_ON | MFRC522_TX2_RF_ON));
    return mfrc522_reg_write(h, MFRC522_REG_TX_CONTROL, value);
}

int mfrc522_set_gain(mfrc522_handle_t *h, uint8_t rx_gain)
{
    uint8_t value = 0U;
    MFRC522_CHECK(h);
    if (rx_gain > 7U) { return (int)HAL_INVALID_PARAM; }
    if (mfrc522_reg_read(h, MFRC522_REG_RF_CFG, &value) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }
    value = (uint8_t)((value & 0x8FU) | (uint8_t)(rx_gain << 4));
    return mfrc522_reg_write(h, MFRC522_REG_RF_CFG, value);
}

static int mfrc522_transceive(mfrc522_handle_t *h, const uint8_t *tx, size_t tx_len,
                              uint8_t *rx, size_t rx_size, size_t *rx_len,
                              uint8_t last_bits)
{
    uint8_t irq = 0U;
    uint8_t err = 0U;
    uint8_t level = 0U;
    uint32_t start;
    size_t got;
    size_t i;

    (void)mfrc522_reg_write(h, MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);
    (void)mfrc522_reg_write(h, MFRC522_REG_COM_IRQ, MFRC522_IRQ_ALL);
    (void)mfrc522_reg_write(h, MFRC522_REG_FIFO_LEVEL, MFRC522_FIFO_FLUSH);
    for (i = 0U; i < tx_len; i++) {
        if (mfrc522_reg_write(h, MFRC522_REG_FIFO_DATA, tx[i]) != HAL_OK) {
            return (int)HAL_IO_ERROR;
        }
    }
    if (mfrc522_reg_write(h, MFRC522_REG_BIT_FRAMING, last_bits) != HAL_OK ||
        mfrc522_reg_write(h, MFRC522_REG_COMMAND, MFRC522_CMD_TRANSCEIVE) != HAL_OK ||
        mfrc522_reg_write(h, MFRC522_REG_BIT_FRAMING,
                          (uint8_t)(last_bits | MFRC522_BITFRAMING_START)) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }
    start = hal_tick_get();
    for (;;) {
        if (mfrc522_reg_read(h, MFRC522_REG_COM_IRQ, &irq) != HAL_OK) {
            return (int)HAL_IO_ERROR;
        }
        /* BitFraming 的 StartSend 位清零表示发送完成 */
        if ((irq & (MFRC522_IRQ_RX | MFRC522_IRQ_TIMER | MFRC522_IRQ_ERR)) != 0U) {
            break;
        }
        if ((uint32_t)(hal_tick_get() - start) > h->timeout_ms) {
            (void)mfrc522_reg_write(h, MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);
            return (int)HAL_TIMEOUT;
        }
        hal_delay_us(50U);
    }
    (void)mfrc522_reg_read(h, MFRC522_REG_ERROR, &err);
    (void)mfrc522_reg_read(h, MFRC522_REG_FIFO_LEVEL, &level);
    got = level;
    if (got > rx_size) { got = rx_size; }
    for (i = 0U; i < got; i++) {
        if (mfrc522_reg_read(h, MFRC522_REG_FIFO_DATA, &rx[i]) != HAL_OK) {
            return (int)HAL_IO_ERROR;
        }
    }
    (void)mfrc522_reg_write(h, MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);
    if (rx_len != NULL) { *rx_len = got; }
    if ((err & (MFRC522_ERR_PROTOCOL | MFRC522_ERR_PARITY | MFRC522_ERR_CRC |
                MFRC522_ERR_OVERFLOW)) != 0U) {
        return (int)HAL_CRC_ERROR;
    }
    return (got == 0U) ? (int)HAL_TIMEOUT : HAL_OK;
}

static int mfrc522_request(mfrc522_handle_t *h, uint8_t atqa[2])
{
    uint8_t tx = MFRC522_PICC_REQA;
    size_t got = 0U;
    if (mfrc522_reg_write(h, MFRC522_REG_BIT_FRAMING, 0x07U) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (mfrc522_transceive(h, &tx, 1U, atqa, 2U, &got, 0x07U) != HAL_OK) {
        return (int)HAL_NOT_FOUND;
    }
    return (got == 2U) ? HAL_OK : (int)HAL_NOT_FOUND;
}

static int mfrc522_anticoll(mfrc522_handle_t *h, uint8_t uid[5])
{
    uint8_t tx[2] = { MFRC522_PICC_ANTICOLL, 0x20U };
    size_t got = 0U;
    if (mfrc522_transceive(h, tx, 2U, uid, 5U, &got, 0x00U) != HAL_OK || got != 5U) {
        return (int)HAL_NOT_FOUND;
    }
    /* BCC 校验 */
    if ((uint8_t)(uid[0] ^ uid[1] ^ uid[2] ^ uid[3]) != uid[4]) {
        return (int)HAL_CRC_ERROR;
    }
    return HAL_OK;
}

static int mfrc522_select(mfrc522_handle_t *h, const uint8_t uid[5], uint8_t *sak)
{
    uint8_t tx[7];
    uint8_t rx[3];
    size_t got = 0U;
    tx[0] = MFRC522_PICC_SELECT;
    tx[1] = 0x70U;
    (void)memcpy(&tx[2], uid, 5U);
    if (mfrc522_transceive(h, tx, sizeof(tx), rx, sizeof(rx), &got, 0x00U) != HAL_OK) {
        return (int)HAL_NOT_FOUND;
    }
    if (got < 3U) { return (int)HAL_NOT_FOUND; }
    *sak = rx[0];
    return HAL_OK;
}

int mfrc522_read(mfrc522_handle_t *h, mfrc522_card_t *card)
{
    uint8_t atqa[2] = { 0U, 0U };
    uint8_t uid[5] = { 0U };
    uint8_t sak = 0U;
    int ret;
    MFRC522_CHECK(h);
    if (card == NULL) { return (int)HAL_INVALID_PARAM; }
    (void)memset(card, 0, sizeof(*card));
    ret = mfrc522_request(h, atqa);
    if (ret != HAL_OK) { return ret; }
    ret = mfrc522_anticoll(h, uid);
    if (ret != HAL_OK) { return ret; }
    ret = mfrc522_select(h, uid, &sak);
    if (ret != HAL_OK) { return ret; }
    (void)memcpy(card->uid, uid, 4U);
    card->uid_len = 4U;
    card->sak = sak;
    card->atqa[0] = atqa[0];
    card->atqa[1] = atqa[1];
    card->valid = true;
    return HAL_OK;
}

int mfrc522_write(mfrc522_handle_t *h, const uint8_t *frame, size_t len,
                  uint8_t *response, size_t response_size, size_t *response_len)
{
    MFRC522_CHECK(h);
    if (frame == NULL || len == 0U || len > MFRC522_FIFO_SIZE) {
        return (int)HAL_INVALID_PARAM;
    }
    if (response != NULL && response_size == 0U) { return (int)HAL_INVALID_PARAM; }
    if (mfrc522_reg_write(h, MFRC522_REG_BIT_FRAMING, 0x00U) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }
    return mfrc522_transceive(h, frame, len, response, response_size, response_len, 0x00U);
}

int mfrc522_halt(mfrc522_handle_t *h)
{
    uint8_t tx[4] = { MFRC522_PICC_HALT, 0x00U, 0x00U, 0x00U };
    uint8_t rx[2];
    size_t got = 0U;
    MFRC522_CHECK(h);
    (void)mfrc522_transceive(h, tx, sizeof(tx), rx, sizeof(rx), &got, 0x00U);
    return HAL_OK;
}

int mfrc522_init(mfrc522_handle_t *handle, hal_spi_t *spi,
                 hal_pin_t cs, hal_pin_t reset)
{
    uint8_t version = 0U;
    if (handle == NULL || spi == NULL) { return (int)HAL_INVALID_PARAM; }
    (void)memset(handle, 0, sizeof(*handle));
    handle->spi.handle = spi;
    handle->spi.cs = cs;
    handle->spi.timeout_ms = HAL_SPI_TIMEOUT_DEFAULT;
    handle->reset = reset;
    handle->timeout_ms = MFRC522_TIMEOUT_MS;
    if (cs != 0U && hal_gpio_config_output(cs, HAL_GPIO_HIGH) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (reset != 0U) {
        if (hal_gpio_config_output(reset, HAL_GPIO_HIGH) != HAL_OK) {
            return (int)HAL_IO_ERROR;
        }
        hal_delay_ms(10U);
        (void)hal_gpio_write(reset, HAL_GPIO_LOW);
        hal_delay_ms(1U);
        (void)hal_gpio_write(reset, HAL_GPIO_HIGH);
        hal_delay_ms(50U);
    }
    /* 软件复位 */
    if (mfrc522_reg_write(handle, MFRC522_REG_COMMAND, MFRC522_CMD_SOFT_RESET) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }
    hal_delay_ms(50U);
    if (mfrc522_reg_read(handle, MFRC522_REG_VERSION, &version) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (version == 0x00U || version == 0xFFU) {
        LOG_E("mfrc522", "not found (version=0x%02X)", version);
        return (int)HAL_NOT_FOUND;
    }
    handle->version = version;
    handle->initialized = true;
    /* 定时器：TAuto=1，40kHz 计数，500us 超时 */
    (void)mfrc522_reg_write(handle, MFRC522_REG_T_MODE, 0x8DU);
    (void)mfrc522_reg_write(handle, MFRC522_REG_T_PRESCALER, 0x3EU);
    (void)mfrc522_reg_write(handle, MFRC522_REG_T_RELOAD_H, 0x00U);
    (void)mfrc522_reg_write(handle, MFRC522_REG_T_RELOAD_L, 0x1EU);
    (void)mfrc522_reg_write(handle, MFRC522_REG_TX_ASK, 0x40U);   /* 100% ASK */
    (void)mfrc522_reg_write(handle, MFRC522_REG_MODE, 0x3DU);     /* CRC 0x6363 */
    if (mfrc522_set_antenna(handle, true) != HAL_OK) {
        handle->initialized = false;
        return (int)HAL_IO_ERROR;
    }
    LOG_I("mfrc522", "init ok, version=0x%02X", version);
    return HAL_OK;
}

int mfrc522_deinit(mfrc522_handle_t *handle)
{
    if (handle == NULL) { return (int)HAL_INVALID_PARAM; }
    if (handle->initialized) {
        (void)mfrc522_set_antenna(handle, false);
        (void)mfrc522_reg_write(handle, MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);
    }
    if (handle->reset != 0U) {
        (void)hal_gpio_write(handle->reset, HAL_GPIO_LOW);
    }
    handle->initialized = false;
    return HAL_OK;
}
