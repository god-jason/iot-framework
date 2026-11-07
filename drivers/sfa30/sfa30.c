#include "sfa30.h"
#include "sfa30_register.h"
#include "log.h"

static uint8_t sfa30_crc(const uint8_t *data)
{
    return driver_crc8(data, 2U, SFA30_CRC_POLY, SFA30_CRC_INIT);
}

static int sfa30_send_cmd(sfa30_handle_t *handle, uint16_t command)
{
    uint8_t buf[3];

    buf[0] = (uint8_t)(command >> 8);
    buf[1] = (uint8_t)(command & 0xFFU);
    buf[2] = sfa30_crc(buf);
    return driver_i2c_write(&handle->i2c, buf, sizeof(buf));
}

/* 命令 + 2 字节参数（参数带 CRC） */
static int sfa30_send_cmd_arg(sfa30_handle_t *handle, uint16_t command,
                              uint8_t arg_hi, uint8_t arg_lo)
{
    uint8_t buf[6];

    buf[0] = (uint8_t)(command >> 8);
    buf[1] = (uint8_t)(command & 0xFFU);
    buf[2] = sfa30_crc(buf);
    buf[3] = arg_hi;
    buf[4] = arg_lo;
    buf[5] = sfa30_crc(&buf[3]);
    return driver_i2c_write(&handle->i2c, buf, sizeof(buf));
}

static int sfa30_read_words(sfa30_handle_t *handle, uint8_t *raw, uint16_t words)
{
    uint16_t i;
    int ret;

    ret = driver_i2c_read(&handle->i2c, raw, (size_t)words * 3U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    for (i = 0U; i < words; i++) {
        if (sfa30_crc(&raw[(size_t)i * 3U]) != raw[(size_t)i * 3U + 2U]) {
            LOG_E("sfa30", "crc error on word %u", (unsigned)i);
            return (int)IOT_CRC_ERROR;
        }
    }
    return DRIVER_OK;
}

static uint16_t sfa30_word_at(const uint8_t *raw, uint16_t index)
{
    size_t off = (size_t)index * 3U;

    return (uint16_t)(((uint16_t)raw[off] << 8) | raw[off + 1U]);
}

int sfa30_init(sfa30_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint64_t serial = 0U;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : SFA30_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->initialized = false;
    handle->measuring = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("sfa30", "probe failed at 0x%02X", (unsigned)handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    ret = sfa30_send_cmd(handle, SFA30_CMD_DEVICE_RESET);
    if (ret != DRIVER_OK) {
        LOG_W("sfa30", "device reset failed: %d", ret);
    }
    driver_delay_ms(SFA30_RESET_DELAY_MS);

    /* ID 校验：序列号 + CRC */
    if (sfa30_get_serial(handle, &serial) != DRIVER_OK || serial == 0U) {
        LOG_E("sfa30", "serial read failed");
        return (int)IOT_NOT_FOUND;
    }

    handle->initialized = true;
    ret = sfa30_start(handle);
    if (ret != DRIVER_OK) {
        LOG_E("sfa30", "start continuous failed: %d", ret);
        handle->initialized = false;
        return ret;
    }

    LOG_I("sfa30", "init ok, serial=0x%08lX", (unsigned long)(serial & 0xFFFFFFFFULL));
    return DRIVER_OK;
}

int sfa30_deinit(sfa30_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (handle->initialized && handle->measuring) {
        (void)sfa30_send_cmd(handle, SFA30_CMD_STOP_CONTINUOUS);
    }
    handle->initialized = false;
    handle->measuring = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int sfa30_start(sfa30_handle_t *handle)
{
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    /* 参数 0x0000 表示使用器件默认测量周期 */
    ret = sfa30_send_cmd_arg(handle, SFA30_CMD_START_CONTINUOUS, 0x00U, 0x00U);
    if (ret == DRIVER_OK) {
        handle->measuring = true;
        driver_delay_ms(SFA30_START_DELAY_MS);
    }
    return ret;
}

int sfa30_stop(sfa30_handle_t *handle)
{
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    ret = sfa30_send_cmd(handle, SFA30_CMD_STOP_CONTINUOUS);
    if (ret == DRIVER_OK) {
        handle->measuring = false;
    }
    return ret;
}

int sfa30_get_serial(sfa30_handle_t *handle, uint64_t *serial)
{
    uint8_t raw[48];
    uint16_t i;
    uint64_t value = 0U;
    int ret;

    if (handle == NULL || serial == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    ret = sfa30_send_cmd(handle, SFA30_CMD_READ_SERIAL);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(5U);
    ret = sfa30_read_words(handle, raw, 16U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    /* 序列号为 ASCII，取前 12 个字符折算成 48 位值（仅用于区分器件） */
    for (i = 0U; i < 12U; i++) {
        value = (value << 4) | (uint64_t)(raw[(size_t)i * 3U] & 0x0FU);
    }
    *serial = value;
    return DRIVER_OK;
}

int sfa30_read(sfa30_handle_t *handle, sfa30_data_t *data)
{
    uint8_t raw[9];
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if (!handle->measuring) {
        ret = sfa30_start(handle);
        if (ret != DRIVER_OK) {
            return ret;
        }
    }

    ret = sfa30_send_cmd(handle, SFA30_CMD_READ_VALUES);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(SFA30_READ_DELAY_MS);
    ret = sfa30_read_words(handle, raw, 3U);
    if (ret != DRIVER_OK) {
        return ret;
    }

    data->hcho_ppb = (float)sfa30_word_at(raw, 0U) / SFA30_HCHO_DIVISOR;
    data->humidity = SFA30_RH_SCALE * (float)sfa30_word_at(raw, 1U) / SFA30_RAW_MAX;
    data->temperature = SFA30_T_OFFSET +
                        SFA30_T_SCALE * (float)sfa30_word_at(raw, 2U) / SFA30_RAW_MAX;
    if (data->humidity > 100.0f) {
        data->humidity = 100.0f;
    }
    return DRIVER_OK;
}
