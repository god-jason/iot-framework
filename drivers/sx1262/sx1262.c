#include <string.h>
#include "sx1262.h"
#include "sx1262_register.h"
#include "log.h"
#define SX126X_CHECK(h) \
    do { if ((h) == NULL || !(h)->initialized) { return (int)IOT_INVALID_PARAM; } } while (0)
static int sx1262_wait_busy(sx1262_handle_t *h)
{
    uint32_t start = driver_tick_ms();
    int level;
    if (h->busy == 0U) { return DRIVER_OK; }
    for (;;) {
        level = driver_gpio_read(h->busy);
        if (level < 0) { return (int)IOT_IO_ERROR; }
        if (level == 0) { return DRIVER_OK; }
        if ((uint32_t)(driver_tick_ms() - start) > h->timeout_ms) {
            LOG_E("sx1262", "busy timeout");
            return (int)IOT_TIMEOUT;
        }
        driver_delay_us(10U);
    }
}

static int sx1262_cmd(sx1262_handle_t *h, uint8_t opcode, const uint8_t *params, size_t len)
{
    uint8_t buf[9];
    if (len > 8U) { return (int)IOT_INVALID_PARAM; }
    if (sx1262_wait_busy(h) != DRIVER_OK) { return (int)IOT_TIMEOUT; }
    buf[0] = opcode; if (len > 0U) { (void)memcpy(&buf[1], params, len); }
    return (driver_spi_write_cs(&h->spi, buf, len + 1U) == DRIVER_OK)
               ? DRIVER_OK : (int)IOT_IO_ERROR;
}

/* 读命令：opcode 之后先返回 1 个状态字节，随后才是数据 */
static int sx1262_rd(sx1262_handle_t *h, uint8_t opcode, uint8_t *data, size_t len)
{
    uint8_t status = 0U;
    if (sx1262_wait_busy(h) != DRIVER_OK) { return (int)IOT_TIMEOUT; }
    driver_spi_select(&h->spi);
    if (driver_spi_write(&h->spi, &opcode, 1U) != DRIVER_OK ||
        driver_spi_read(&h->spi, &status, 1U) != DRIVER_OK ||
        driver_spi_read(&h->spi, data, len) != DRIVER_OK) {
        driver_spi_deselect(&h->spi);
        return (int)IOT_IO_ERROR;
    }
    driver_spi_deselect(&h->spi);
    return DRIVER_OK;
}

static int sx1262_clear_irq(sx1262_handle_t *h)
{
    static const uint8_t all[2] = { 0xFFU, 0xFFU };
    return sx1262_cmd(h, SX126X_CLEAR_IRQ_STATUS, all, 2U);
}

static void sx1262_calibrate_image(sx1262_handle_t *h)
{
    uint8_t cal[2] = { 0xD7U, 0xDBU };   /* 863..870MHz 默认 */
    if (h->frequency_hz < 470000000U) { cal[0] = 0x6BU; cal[1] = 0x6FU; }
    else if (h->frequency_hz < 779000000U) { cal[0] = 0x75U; cal[1] = 0x81U; }
    else if (h->frequency_hz < 902000000U) { cal[0] = 0xC1U; cal[1] = 0xC5U; }
    else { cal[0] = 0xE1U; cal[1] = 0xE9U; }
    (void)sx1262_cmd(h, SX126X_CALIBRATE_IMAGE, cal, 2U);
}

static void sx1262_timeout_code(uint32_t ms, uint8_t out[3])
{
    uint32_t units = ms * 64U;   /* 单位 15.625us */
    if (units > 0xFFFFFFU) { units = 0xFFFFFFU; }
    out[0] = (uint8_t)(units >> 16); out[1] = (uint8_t)(units >> 8); out[2] = (uint8_t)units;
}

int sx1262_set_frequency(sx1262_handle_t *h, uint32_t frequency_hz)
{
    uint8_t p[4];
    uint32_t rf;
    SX126X_CHECK(h);
    if (frequency_hz < ((h->variant == SX1262_VARIANT_SX1268) ? SX1268_FREQ_MIN_HZ
                                                               : SX1262_FREQ_MIN_HZ) ||
        frequency_hz > ((h->variant == SX1262_VARIANT_SX1268) ? SX1268_FREQ_MAX_HZ
                                                               : SX1262_FREQ_MAX_HZ)) {
        return (int)IOT_INVALID_PARAM;
    }
    rf = (uint32_t)(((uint64_t)frequency_hz * SX126X_FREQ_MUL) / SX126X_XTAL_FREQ_HZ);
    p[0] = (uint8_t)(rf >> 24); p[1] = (uint8_t)(rf >> 16);
    p[2] = (uint8_t)(rf >> 8); p[3] = (uint8_t)rf;
    if (sx1262_cmd(h, SX126X_SET_RF_FREQUENCY, p, 4U) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    h->frequency_hz = frequency_hz;
    return DRIVER_OK;
}

int sx1262_set_modulation(sx1262_handle_t *h, uint8_t sf, uint32_t bw_hz, uint8_t cr)
{
    uint8_t p[4];
    SX126X_CHECK(h);
    if (sf < 5U || sf > 12U || cr < 1U || cr > 4U) { return (int)IOT_INVALID_PARAM; }
    p[1] = (bw_hz == 125000U) ? SX126X_BW_125KHZ
         : (bw_hz == 250000U) ? SX126X_BW_250KHZ
         : (bw_hz == 500000U) ? SX126X_BW_500KHZ : 0xFFU;
    if (p[1] == 0xFFU) { return (int)IOT_INVALID_PARAM; }
    p[0] = sf; p[2] = cr;
    p[3] = (sf >= 11U && bw_hz == 125000U) ? 1U : 0U;   /* LDRO */
    if (sx1262_cmd(h, SX126X_SET_MODULATION_PARAMS, p, 4U) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    h->spreading_factor = sf;
    h->bandwidth_hz = bw_hz;
    h->coding_rate = cr;
    return DRIVER_OK;
}

int sx1262_set_mode(sx1262_handle_t *h, sx1262_mode_t mode)
{
    uint8_t p[3] = { 0U, 0U, 0U };
    SX126X_CHECK(h);
    if (mode == SX1262_MODE_STANDBY) { return sx1262_cmd(h, SX126X_SET_STANDBY, p, 1U); }
    if (mode == SX1262_MODE_SLEEP) {   /* warm start */
        p[0] = 0x04U;
        return sx1262_cmd(h, SX126X_SET_SLEEP, p, 1U);
    }
    if (mode == SX1262_MODE_TX) {
        sx1262_timeout_code(h->timeout_ms, p);
        return sx1262_cmd(h, SX126X_SET_TX, p, 3U);
    }
    if (mode == SX1262_MODE_RX) {   /* 连续接收 */
        p[0] = 0xFFU; p[1] = 0xFFU; p[2] = 0xFFU;
        return sx1262_cmd(h, SX126X_SET_RX, p, 3U);
    }
    return (int)IOT_INVALID_PARAM;
}

int sx1262_send(sx1262_handle_t *h, const uint8_t *data, size_t len)
{
    uint8_t p[8];
    uint8_t irq[2] = { 0U, 0U };
    uint32_t start;
    SX126X_CHECK(h);
    if (data == NULL || len == 0U || len > SX126X_PAYLOAD_MAX) {
        return (int)IOT_INVALID_PARAM;
    }
    p[0] = SX126X_STDBY_RC;   /* 发送前回到 STDBY_RC */
    if (sx1262_cmd(h, SX126X_SET_STANDBY, p, 1U) != DRIVER_OK) { return (int)IOT_IO_ERROR; }
    /* WriteBuffer：opcode + offset + payload */
    if (sx1262_wait_busy(h) != DRIVER_OK) { return (int)IOT_TIMEOUT; }
    p[0] = SX126X_WRITE_BUFFER; p[1] = 0x00U;
    driver_spi_select(&h->spi);
    if (driver_spi_write(&h->spi, p, 2U) != DRIVER_OK ||
        driver_spi_write(&h->spi, data, len) != DRIVER_OK) {
        driver_spi_deselect(&h->spi);
        return (int)IOT_IO_ERROR;
    }
    driver_spi_deselect(&h->spi);
    /* SetPacketParams：前导码/显式包头/长度/CRC 开/IQ 不反转 */
    p[0] = (uint8_t)(h->preamble_len >> 8); p[1] = (uint8_t)h->preamble_len;
    p[2] = 0x00U; p[3] = (uint8_t)len; p[4] = 0x01U; p[5] = 0x00U;
    if (sx1262_cmd(h, SX126X_SET_PACKET_PARAMS, p, 6U) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    (void)sx1262_clear_irq(h);
    sx1262_timeout_code(h->timeout_ms, p);
    if (sx1262_cmd(h, SX126X_SET_TX, p, 3U) != DRIVER_OK) { return (int)IOT_IO_ERROR; }
    start = driver_tick_ms();
    for (;;) {
        if (sx1262_rd(h, SX126X_GET_IRQ_STATUS, irq, 2U) != DRIVER_OK) {
            return (int)IOT_IO_ERROR;
        }
        if ((irq[0] & 0x01U) != 0U) {          /* TxDone */
            (void)sx1262_clear_irq(h);
            return DRIVER_OK;
        }
        if ((irq[1] & 0x02U) != 0U) {          /* Timeout */
            (void)sx1262_clear_irq(h);
            return (int)IOT_TIMEOUT;
        }
        if ((uint32_t)(driver_tick_ms() - start) > h->timeout_ms + 50U) {
            return (int)IOT_TIMEOUT;
        }
        driver_delay_us(100U);
    }
}

int sx1262_poll(sx1262_handle_t *h, uint8_t *data, size_t len, size_t *received)
{
    uint8_t irq[2] = { 0U, 0U };
    uint8_t st[2] = { 0U, 0U };
    SX126X_CHECK(h);
    if (data == NULL || len == 0U) { return (int)IOT_INVALID_PARAM; }
    if (sx1262_rd(h, SX126X_GET_IRQ_STATUS, irq, 2U) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    if ((irq[1] & 0x02U) != 0U) {              /* Timeout */
        (void)sx1262_clear_irq(h);
        return (int)IOT_TIMEOUT;
    }
    if ((irq[0] & 0x02U) == 0U) { return (int)IOT_NOT_READY; }
    if ((irq[0] & 0x40U) != 0U) {              /* CrcErr */
        (void)sx1262_clear_irq(h);
        return (int)IOT_CRC_ERROR;
    }
    if (sx1262_rd(h, SX126X_GET_RX_BUFFER_STATUS, st, 2U) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    if (len < st[0]) {
        (void)sx1262_clear_irq(h);
        return (int)IOT_OVERFLOW;
    }
    if (sx1262_wait_busy(h) != DRIVER_OK) { return (int)IOT_TIMEOUT; }
    irq[0] = SX126X_READ_BUFFER; irq[1] = st[1];
    driver_spi_select(&h->spi);
    if (driver_spi_write(&h->spi, irq, 2U) != DRIVER_OK ||
        driver_spi_read(&h->spi, data, st[0]) != DRIVER_OK) {
        driver_spi_deselect(&h->spi);
        return (int)IOT_IO_ERROR;
    }
    driver_spi_deselect(&h->spi);
    (void)sx1262_clear_irq(h);
    if (received != NULL) { *received = st[0]; }
    return DRIVER_OK;
}

int sx1262_init_variant(sx1262_handle_t *handle, iot_spi_t *spi, iot_pin_t cs,
                        iot_pin_t busy, iot_pin_t reset, iot_pin_t dio1,
                        sx1262_variant_t variant)
{
    uint8_t p[8];
    if (handle == NULL || spi == NULL || busy == 0U || reset == 0U) {
        return (int)IOT_INVALID_PARAM;
    }
    (void)memset(handle, 0, sizeof(*handle));
    handle->spi.handle = spi; handle->spi.cs = cs;
    handle->spi.timeout_ms = DRIVER_SPI_TIMEOUT_DEFAULT;
    handle->busy = busy; handle->reset = reset; handle->dio1 = dio1;
    handle->variant = variant;
    handle->frequency_hz = (variant == SX1262_VARIANT_SX1268) ? 433920000U : 868000000U;
    handle->spreading_factor = 7U; handle->bandwidth_hz = 125000U;
    handle->coding_rate = 1U; handle->tx_power_dbm = 14;
    handle->preamble_len = 8U; handle->timeout_ms = 1000U;
    if ((cs != 0U && driver_gpio_output(cs, IOT_GPIO_HIGH) != DRIVER_OK) ||
        driver_gpio_output(reset, IOT_GPIO_HIGH) != DRIVER_OK ||
        driver_gpio_input(busy, IOT_GPIO_NOPULL) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    if (dio1 != 0U && driver_gpio_input(dio1, IOT_GPIO_NOPULL) != DRIVER_OK) { return (int)IOT_IO_ERROR; }
    (void)driver_gpio_write(reset, IOT_GPIO_LOW); driver_delay_ms(1U);   /* 硬复位 */
    (void)driver_gpio_write(reset, IOT_GPIO_HIGH); driver_delay_ms(10U);
    if (sx1262_wait_busy(handle) != DRIVER_OK) {
        LOG_E("sx1262", "reset failed"); return (int)IOT_NOT_FOUND;
    }
    handle->initialized = true;
    p[0] = SX126X_REGULATOR_DC_DC;
    (void)sx1262_cmd(handle, SX126X_SET_REGULATOR_MODE, p, 1U);
    p[0] = 0x01U;   /* DIO2 驱动射频开关 */
    (void)sx1262_cmd(handle, SX126X_SET_DIO2_AS_RF_SWITCH, p, 1U);
    p[0] = SX126X_STDBY_RC;
    (void)sx1262_cmd(handle, SX126X_SET_STANDBY, p, 1U);
    sx1262_calibrate_image(handle);
    p[0] = SX126X_PACKET_TYPE_LORA;
    (void)sx1262_cmd(handle, SX126X_SET_PACKET_TYPE, p, 1U);
    /* SPI 自检：回读包类型，失败说明总线/器件异常 */
    if (sx1262_rd(handle, SX126X_GET_PACKET_TYPE, p, 1U) != DRIVER_OK ||
        p[0] != SX126X_PACKET_TYPE_LORA) {
        handle->initialized = false;
        LOG_E("sx1262", "device not responding");
        return (int)IOT_NOT_FOUND;
    }
    /* PA：dutyCycle=0x04, hpMax=0x07, deviceSel=0x00(SX1262), paLut=0x01 */
    p[0] = 0x04U; p[1] = 0x07U; p[2] = 0x00U; p[3] = 0x01U;
    (void)sx1262_cmd(handle, SX126X_SET_PA_CONFIG, p, 4U);
    p[0] = 0x00U; p[1] = 0x00U;   /* TX/RX buffer 基址 */
    (void)sx1262_cmd(handle, SX126X_SET_BUFFER_BASE_ADDR, p, 2U);
    /* 全局/DIO1 中断：TxDone|RxDone|CrcErr|Timeout */
    p[0] = 0x43U; p[1] = 0x02U; p[2] = 0x43U; p[3] = 0x02U;
    p[4] = 0x00U; p[5] = 0x00U; p[6] = 0x00U; p[7] = 0x00U;
    (void)sx1262_cmd(handle, SX126X_SET_DIO_IRQ_PARAMS, p, 8U);
    p[0] = (uint8_t)handle->tx_power_dbm; p[1] = SX126X_PA_RAMP_200US;
    (void)sx1262_cmd(handle, SX126X_SET_TX_PARAMS, p, 2U);
    if (sx1262_set_frequency(handle, handle->frequency_hz) != DRIVER_OK ||
        sx1262_set_modulation(handle, handle->spreading_factor, handle->bandwidth_hz,
                              handle->coding_rate) != DRIVER_OK) {
        handle->initialized = false;
        return (int)IOT_IO_ERROR;
    }
    LOG_I("sx1262", "ok, variant=%d, %u Hz", (int)variant, (unsigned)handle->frequency_hz);
    return DRIVER_OK;
}

int sx1262_init(sx1262_handle_t *handle, iot_spi_t *spi, iot_pin_t cs,
                iot_pin_t busy, iot_pin_t reset, iot_pin_t dio1)
{
    return sx1262_init_variant(handle, spi, cs, busy, reset, dio1, SX1262_VARIANT_SX1262);
}

int sx1262_deinit(sx1262_handle_t *handle)
{
    if (handle == NULL) { return (int)IOT_INVALID_PARAM; }
    if (handle->initialized) { (void)sx1262_set_mode(handle, SX1262_MODE_SLEEP); }
    handle->initialized = false; return DRIVER_OK;
}
