#include <string.h>
#include "mfrc522.h"
#include "mfrc522_register.h"
#include "log.h"

#define MFRC522_CHECK(h) \
    do { if ((h) == NULL || !(h)->initialized) { return (int)IOT_INVALID_PARAM; } } while (0)

static int mfrc522_reg_read(mfrc522_handle_t *h, uint8_t reg, uint8_t *value)
{
    uint8_t addr = (uint8_t)(((reg << 1) & 0x7EU) | 0x80U);
    driver_spi_select(&h->spi);
    if (driver_spi_write(&h->spi, &addr, 1U) != DRIVER_OK ||
        driver_spi_read(&h->spi, value, 1U) != DRIVER_OK) {
        driver_spi_deselect(&h->spi);
        return (int)IOT_IO_ERROR;
    }
    driver_spi_deselect(&h->spi);
    return DRIVER_OK;
}

static int mfrc522_reg_write(mfrc522_handle_t *h, uint8_t reg, uint8_t value)
{
    uint8_t buf[2];
    buf[0] = (uint8_t)((reg << 1) & 0x7EU);
    buf[1] = value;
    return (driver_spi_write_cs(&h->spi, buf, 2U) == DRIVER_OK)
               ? DRIVER_OK : (int)IOT_IO_ERROR;
}

static int mfrc522_set_bits(mfrc522_handle_t *h, uint8_t reg, uint8_t mask)
{
    uint8_t value = 0U;
    if (mfrc522_reg_read(h, reg, &value) != DRIVER_OK) { return (int)IOT_IO_ERROR; }
    return mfrc522_reg_write(h, reg, (uint8_t)(value | mask));
}

int mfrc522_get_version(mfrc522_handle_t *h, uint8_t *version)
{
    MFRC522_CHECK(h);
    if (version == NULL) { return (int)IOT_INVALID_PARAM; }
    return mfrc522_reg_read(h, MFRC522_REG_VERSION, version);
}

int mfrc522_set_antenna(mfrc522_handle_t *h, bool on)
{
    uint8_t value = 0U;
    MFRC522_CHECK(h);
    if (on) {
        if (mfrc522_reg_read(h, MFRC522_REG_TX_CONTROL, &value) != DRIVER_OK) {
            return (int)IOT_IO_ERROR;
        }
        if ((value & (MFRC522_TX1_RF_ON | MFRC522_TX2_RF_ON)) == 0U) {
            return mfrc522_set_bits(h, MFRC522_REG_TX_CONTROL,
                                    MFRC522_TX1_RF_ON | MFRC522_TX2_RF_ON);
        }
        return DRIVER_OK;
    }
    if (mfrc522_reg_read(h, MFRC522_REG_TX_CONTROL, &value) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    value = (uint8_t)(value & (uint8_t)~(MFRC522_TX1_RF_ON | MFRC522_TX2_RF_ON));
    return mfrc522_reg_write(h, MFRC522_REG_TX_CONTROL, value);
}

int mfrc522_set_gain(mfrc522_handle_t *h, uint8_t rx_gain)
{
    uint8_t value = 0U;
    MFRC522_CHECK(h);
    if (rx_gain > 7U) { return (int)IOT_INVALID_PARAM; }
    if (mfrc522_reg_read(h, MFRC522_REG_RF_CFG, &value) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
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
        if (mfrc522_reg_write(h, MFRC522_REG_FIFO_DATA, tx[i]) != DRIVER_OK) {
            return (int)IOT_IO_ERROR;
        }
    }
    if (mfrc522_reg_write(h, MFRC522_REG_BIT_FRAMING, last_bits) != DRIVER_OK ||
        mfrc522_reg_write(h, MFRC522_REG_COMMAND, MFRC522_CMD_TRANSCEIVE) != DRIVER_OK ||
        mfrc522_reg_write(h, MFRC522_REG_BIT_FRAMING,
                          (uint8_t)(last_bits | MFRC522_BITFRAMING_START)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    start = driver_tick_ms();
    for (;;) {
        if (mfrc522_reg_read(h, MFRC522_REG_COM_IRQ, &irq) != DRIVER_OK) {
            return (int)IOT_IO_ERROR;
        }
        /* BitFraming 的 StartSend 位清零表示发送完成 */
        if ((irq & (MFRC522_IRQ_RX | MFRC522_IRQ_TIMER | MFRC522_IRQ_ERR)) != 0U) {
            break;
        }
        if ((uint32_t)(driver_tick_ms() - start) > h->timeout_ms) {
            (void)mfrc522_reg_write(h, MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);
            return (int)IOT_TIMEOUT;
        }
        driver_delay_us(50U);
    }
    (void)mfrc522_reg_read(h, MFRC522_REG_ERROR, &err);
    (void)mfrc522_reg_read(h, MFRC522_REG_FIFO_LEVEL, &level);
    got = level;
    if (got > rx_size) { got = rx_size; }
    for (i = 0U; i < got; i++) {
        if (mfrc522_reg_read(h, MFRC522_REG_FIFO_DATA, &rx[i]) != DRIVER_OK) {
            return (int)IOT_IO_ERROR;
        }
    }
    (void)mfrc522_reg_write(h, MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);
    if (rx_len != NULL) { *rx_len = got; }
    if ((err & (MFRC522_ERR_PROTOCOL | MFRC522_ERR_PARITY | MFRC522_ERR_CRC |
                MFRC522_ERR_OVERFLOW)) != 0U) {
        return (int)IOT_CRC_ERROR;
    }
    return (got == 0U) ? (int)IOT_TIMEOUT : DRIVER_OK;
}

static int mfrc522_request(mfrc522_handle_t *h, uint8_t atqa[2])
{
    uint8_t tx = MFRC522_PICC_REQA;
    size_t got = 0U;
    if (mfrc522_reg_write(h, MFRC522_REG_BIT_FRAMING, 0x07U) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    if (mfrc522_transceive(h, &tx, 1U, atqa, 2U, &got, 0x07U) != DRIVER_OK) {
        return (int)IOT_NOT_FOUND;
    }
    return (got == 2U) ? DRIVER_OK : (int)IOT_NOT_FOUND;
}

static int mfrc522_anticoll(mfrc522_handle_t *h, uint8_t uid[5])
{
    uint8_t tx[2] = { MFRC522_PICC_ANTICOLL, 0x20U };
    size_t got = 0U;
    if (mfrc522_transceive(h, tx, 2U, uid, 5U, &got, 0x00U) != DRIVER_OK || got != 5U) {
        return (int)IOT_NOT_FOUND;
    }
    /* BCC 校验 */
    if ((uint8_t)(uid[0] ^ uid[1] ^ uid[2] ^ uid[3]) != uid[4]) {
        return (int)IOT_CRC_ERROR;
    }
    return DRIVER_OK;
}

static int mfrc522_select(mfrc522_handle_t *h, const uint8_t uid[5], uint8_t *sak)
{
    uint8_t tx[7];
    uint8_t rx[3];
    size_t got = 0U;
    tx[0] = MFRC522_PICC_SELECT;
    tx[1] = 0x70U;
    (void)memcpy(&tx[2], uid, 5U);
    if (mfrc522_transceive(h, tx, sizeof(tx), rx, sizeof(rx), &got, 0x00U) != DRIVER_OK) {
        return (int)IOT_NOT_FOUND;
    }
    if (got < 3U) { return (int)IOT_NOT_FOUND; }
    *sak = rx[0];
    return DRIVER_OK;
}

int mfrc522_read(mfrc522_handle_t *h, mfrc522_card_t *card)
{
    uint8_t atqa[2] = { 0U, 0U };
    uint8_t uid[5] = { 0U };
    uint8_t sak = 0U;
    int ret;
    MFRC522_CHECK(h);
    if (card == NULL) { return (int)IOT_INVALID_PARAM; }
    (void)memset(card, 0, sizeof(*card));
    ret = mfrc522_request(h, atqa);
    if (ret != DRIVER_OK) { return ret; }
    ret = mfrc522_anticoll(h, uid);
    if (ret != DRIVER_OK) { return ret; }
    ret = mfrc522_select(h, uid, &sak);
    if (ret != DRIVER_OK) { return ret; }
    (void)memcpy(card->uid, uid, 4U);
    card->uid_len = 4U;
    card->sak = sak;
    card->atqa[0] = atqa[0];
    card->atqa[1] = atqa[1];
    card->valid = true;
    return DRIVER_OK;
}

int mfrc522_write(mfrc522_handle_t *h, const uint8_t *frame, size_t len,
                  uint8_t *response, size_t response_size, size_t *response_len)
{
    MFRC522_CHECK(h);
    if (frame == NULL || len == 0U || len > MFRC522_FIFO_SIZE) {
        return (int)IOT_INVALID_PARAM;
    }
    if (response != NULL && response_size == 0U) { return (int)IOT_INVALID_PARAM; }
    if (mfrc522_reg_write(h, MFRC522_REG_BIT_FRAMING, 0x00U) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
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
    return DRIVER_OK;
}

int mfrc522_init(mfrc522_handle_t *handle, iot_spi_t *spi,
                 iot_pin_t cs, iot_pin_t reset)
{
    uint8_t version = 0U;
    if (handle == NULL || spi == NULL) { return (int)IOT_INVALID_PARAM; }
    (void)memset(handle, 0, sizeof(*handle));
    handle->spi.handle = spi;
    handle->spi.cs = cs;
    handle->spi.timeout_ms = DRIVER_SPI_TIMEOUT_DEFAULT;
    handle->reset = reset;
    handle->timeout_ms = MFRC522_TIMEOUT_MS;
    if (cs != 0U && driver_gpio_output(cs, IOT_GPIO_HIGH) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    if (reset != 0U) {
        if (driver_gpio_output(reset, IOT_GPIO_HIGH) != DRIVER_OK) {
            return (int)IOT_IO_ERROR;
        }
        driver_delay_ms(10U);
        (void)driver_gpio_write(reset, IOT_GPIO_LOW);
        driver_delay_ms(1U);
        (void)driver_gpio_write(reset, IOT_GPIO_HIGH);
        driver_delay_ms(50U);
    }
    /* 软件复位 */
    if (mfrc522_reg_write(handle, MFRC522_REG_COMMAND, MFRC522_CMD_SOFT_RESET) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(50U);
    if (mfrc522_reg_read(handle, MFRC522_REG_VERSION, &version) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    if (version == 0x00U || version == 0xFFU) {
        LOG_E("mfrc522", "not found (version=0x%02X)", version);
        return (int)IOT_NOT_FOUND;
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
    if (mfrc522_set_antenna(handle, true) != DRIVER_OK) {
        handle->initialized = false;
        return (int)IOT_IO_ERROR;
    }
    LOG_I("mfrc522", "init ok, version=0x%02X", version);
    return DRIVER_OK;
}

int mfrc522_deinit(mfrc522_handle_t *handle)
{
    if (handle == NULL) { return (int)IOT_INVALID_PARAM; }
    if (handle->initialized) {
        (void)mfrc522_set_antenna(handle, false);
        (void)mfrc522_reg_write(handle, MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);
    }
    if (handle->reset != 0U) {
        (void)driver_gpio_write(handle->reset, IOT_GPIO_LOW);
    }
    handle->initialized = false;
    return DRIVER_OK;
}
