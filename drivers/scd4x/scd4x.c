#include "scd4x.h"
#include "scd4x_register.h"
#include "log.h"

static uint8_t scd4x_crc(const uint8_t *data)
{
    return driver_crc8(data, 2U, SCD4X_CRC_POLY, SCD4X_CRC_INIT);
}

/* 命令字 + CRC（SCD4x 的命令也带 CRC） */
static int scd4x_send_cmd(scd4x_handle_t *handle, uint16_t command)
{
    uint8_t buf[3];

    buf[0] = (uint8_t)(command >> 8);
    buf[1] = (uint8_t)(command & 0xFFU);
    buf[2] = scd4x_crc(buf);
    return driver_i2c_write(&handle->i2c, buf, sizeof(buf));
}

/* 命令 + 1 个 16 位参数（参数带 CRC） */
static int scd4x_send_cmd_u16(scd4x_handle_t *handle, uint16_t command, uint16_t value)
{
    uint8_t buf[6];

    buf[0] = (uint8_t)(command >> 8);
    buf[1] = (uint8_t)(command & 0xFFU);
    buf[2] = scd4x_crc(buf);
    buf[3] = (uint8_t)(value >> 8);
    buf[4] = (uint8_t)(value & 0xFFU);
    buf[5] = scd4x_crc(&buf[3]);
    return driver_i2c_write(&handle->i2c, buf, sizeof(buf));
}

static int scd4x_read_words(scd4x_handle_t *handle, uint8_t *raw, uint8_t words)
{
    uint8_t i;
    int ret;

    ret = driver_i2c_read(&handle->i2c, raw, (size_t)words * 3U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    for (i = 0U; i < words; i++) {
        if (scd4x_crc(&raw[(size_t)i * 3U]) != raw[(size_t)i * 3U + 2U]) {
            LOG_E("scd4x", "crc error on word %u", (unsigned)i);
            return (int)IOT_CRC_ERROR;
        }
    }
    return DRIVER_OK;
}

static uint16_t scd4x_word_at(const uint8_t *raw, uint8_t index)
{
    size_t off = (size_t)index * 3U;

    return (uint16_t)(((uint16_t)raw[off] << 8) | raw[off + 1U]);
}

int scd4x_init(scd4x_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint64_t serial = 0U;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : SCD4X_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("scd4x", "probe failed at 0x%02X", (unsigned)handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    /* 唤醒（器件可能处于低功耗），失败不视为致命 */
    (void)scd4x_send_cmd(handle, SCD4X_CMD_WAKE_UP);
    driver_delay_ms(SCD4X_WAKE_DELAY_MS);

    /* 若已在周期测量中，先停止；器件忙碌时该命令可能失败 */
    ret = scd4x_send_cmd(handle, SCD4X_CMD_STOP_PERIODIC);
    if (ret == DRIVER_OK) {
        driver_delay_ms(SCD4X_STOP_DELAY_MS);
    }

    /* ID 校验：序列号 + CRC */
    if (scd4x_get_serial(handle, &serial) != DRIVER_OK || serial == 0U) {
        LOG_E("scd4x", "serial read failed");
        return (int)IOT_NOT_FOUND;
    }

    ret = scd4x_send_cmd(handle, SCD4X_CMD_START_PERIODIC);
    if (ret != DRIVER_OK) {
        LOG_E("scd4x", "start periodic failed: %d", ret);
        return ret;
    }
    driver_delay_ms(SCD4X_READ_DELAY_MS);

    handle->initialized = true;
    LOG_I("scd4x", "init ok, serial=0x%04X%08lX",
          (unsigned)(uint16_t)(serial >> 32), (unsigned long)(serial & 0xFFFFFFFFULL));
    return DRIVER_OK;
}

int scd4x_deinit(scd4x_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)scd4x_send_cmd(handle, SCD4X_CMD_STOP_PERIODIC);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int scd4x_get_serial(scd4x_handle_t *handle, uint64_t *serial)
{
    uint8_t raw[9];
    int ret;

    if (handle == NULL || serial == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    ret = scd4x_send_cmd(handle, SCD4X_CMD_GET_SERIAL);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(SCD4X_READ_DELAY_MS);
    ret = scd4x_read_words(handle, raw, 3U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *serial = ((uint64_t)scd4x_word_at(raw, 0U) << 32) |
              ((uint64_t)scd4x_word_at(raw, 1U) << 16) |
              (uint64_t)scd4x_word_at(raw, 2U);
    return DRIVER_OK;
}

int scd4x_stop(scd4x_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return scd4x_send_cmd(handle, SCD4X_CMD_STOP_PERIODIC);
}

int scd4x_reinit(scd4x_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return scd4x_send_cmd(handle, SCD4X_CMD_REINIT);
}

int scd4x_power_down(scd4x_handle_t *handle)
{
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    ret = scd4x_send_cmd(handle, SCD4X_CMD_POWER_DOWN);
    if (ret == DRIVER_OK) {
        handle->initialized = false;
    }
    return ret;
}

int scd4x_set_temperature_offset(scd4x_handle_t *handle, uint16_t offset_centi_c)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return scd4x_send_cmd_u16(handle, SCD4X_CMD_SET_TEMP_OFFSET, offset_centi_c);
}

int scd4x_set_altitude(scd4x_handle_t *handle, uint16_t altitude_m)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return scd4x_send_cmd_u16(handle, SCD4X_CMD_SET_ALTITUDE, altitude_m);
}

int scd4x_force_recalibration(scd4x_handle_t *handle, uint16_t reference_ppm)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return scd4x_send_cmd_u16(handle, SCD4X_CMD_FORCED_RECAL, reference_ppm);
}

int scd4x_read(scd4x_handle_t *handle, scd4x_data_t *data)
{
    uint8_t ready[3];
    uint8_t raw[9];
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = scd4x_send_cmd(handle, SCD4X_CMD_GET_DATA_READY);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(SCD4X_READ_DELAY_MS);
    ret = scd4x_read_words(handle, ready, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    /* 数据就绪状态为 23 位，低 11 位保留，非 0 即就绪 */
    if (((((uint16_t)ready[0] << 8) | ready[1]) & 0x07FFU) == 0U) {
        return (int)IOT_NOT_READY;
    }

    ret = scd4x_send_cmd(handle, SCD4X_CMD_READ_MEASUREMENT);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(SCD4X_READ_DELAY_MS);
    ret = scd4x_read_words(handle, raw, 3U);
    if (ret != DRIVER_OK) {
        return ret;
    }

    data->co2_ppm = scd4x_word_at(raw, 0U);
    data->temperature = SCD4X_TEMP_OFFSET +
                        SCD4X_TEMP_SCALE * (float)scd4x_word_at(raw, 1U) / SCD4X_RAW_MAX;
    data->humidity = SCD4X_HUMIDITY_SCALE * (float)scd4x_word_at(raw, 2U) / SCD4X_RAW_MAX;
    return DRIVER_OK;
}
