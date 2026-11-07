#include "scd30.h"
#include "scd30_register.h"
#include "log.h"

#include <string.h>

/* SCD30 数据字 CRC：poly 0x31, init 0xFF */
static uint8_t scd30_crc(const uint8_t *data)
{
    return driver_crc8(data, 2U, SCD30_CRC_POLY, SCD30_CRC_INIT);
}

/* 大端 IEEE754 float -> 主机 float（假定小端主机，字节序反转后 memcpy） */
static float scd30_be_float(const uint8_t *p)
{
    uint8_t b[4];
    float value;

    b[0] = p[3];
    b[1] = p[2];
    b[2] = p[1];
    b[3] = p[0];
    memcpy(&value, b, sizeof(value));
    return value;
}

static int scd30_send_cmd(scd30_handle_t *handle, uint16_t command)
{
    uint8_t buf[2];

    buf[0] = (uint8_t)(command >> 8);
    buf[1] = (uint8_t)(command & 0xFFU);
    return driver_i2c_write(&handle->i2c, buf, sizeof(buf));
}

/* 命令 + 1 个 16 位参数（参数需 CRC） */
static int scd30_send_cmd_u16(scd30_handle_t *handle, uint16_t command, uint16_t value)
{
    uint8_t buf[5];

    buf[0] = (uint8_t)(command >> 8);
    buf[1] = (uint8_t)(command & 0xFFU);
    buf[2] = (uint8_t)(value >> 8);
    buf[3] = (uint8_t)(value & 0xFFU);
    buf[4] = scd30_crc(&buf[2]);
    return driver_i2c_write(&handle->i2c, buf, sizeof(buf));
}

/* 读 words 个数据字并逐字校验 CRC */
static int scd30_read_words(scd30_handle_t *handle, uint8_t *raw, uint8_t words)
{
    uint8_t i;
    int ret;

    ret = driver_i2c_read(&handle->i2c, raw, (size_t)words * 3U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    for (i = 0U; i < words; i++) {
        if (scd30_crc(&raw[(size_t)i * 3U]) != raw[(size_t)i * 3U + 2U]) {
            LOG_E("scd30", "crc error on word %u", (unsigned)i);
            return (int)IOT_CRC_ERROR;
        }
    }
    return DRIVER_OK;
}

int scd30_init(scd30_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint16_t version = 0U;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : SCD30_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->interval_s = SCD30_DEFAULT_INTERVAL_S;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("scd30", "probe failed at 0x%02X", (unsigned)handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    ret = scd30_send_cmd(handle, SCD30_CMD_SOFT_RESET);
    if (ret != DRIVER_OK) {
        LOG_W("scd30", "soft reset failed: %d", ret);
    }
    driver_delay_ms(SCD30_RESET_DELAY_MS);

    /* ID 校验：读固件版本（0 或读取失败视为器件异常） */
    if (scd30_get_firmware_version(handle, &version) != DRIVER_OK) {
        LOG_E("scd30", "firmware version read failed");
        return (int)IOT_NOT_FOUND;
    }

    ret = scd30_send_cmd_u16(handle, SCD30_CMD_TRIGGER_CONTINUOUS, handle->interval_s);
    if (ret != DRIVER_OK) {
        LOG_E("scd30", "start continuous failed: %d", ret);
        return ret;
    }
    driver_delay_ms(SCD30_START_DELAY_MS);

    handle->initialized = true;
    LOG_I("scd30", "init ok, fw=%u, interval=%us",
          (unsigned)version, (unsigned)handle->interval_s);
    return DRIVER_OK;
}

int scd30_deinit(scd30_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)scd30_send_cmd(handle, SCD30_CMD_STOP_CONTINUOUS);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int scd30_stop(scd30_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return scd30_send_cmd(handle, SCD30_CMD_STOP_CONTINUOUS);
}

int scd30_set_interval(scd30_handle_t *handle, uint16_t interval_s)
{
    int ret;

    if (handle == NULL || !handle->initialized || interval_s < 2U || interval_s > 1800U) {
        return (int)IOT_INVALID_PARAM;
    }
    ret = scd30_send_cmd(handle, SCD30_CMD_STOP_CONTINUOUS);
    if (ret != DRIVER_OK) {
        return ret;
    }
    ret = scd30_send_cmd_u16(handle, SCD30_CMD_TRIGGER_CONTINUOUS, interval_s);
    if (ret == DRIVER_OK) {
        handle->interval_s = interval_s;
    }
    return ret;
}

int scd30_set_temperature_offset(scd30_handle_t *handle, uint16_t offset_centi_c)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return scd30_send_cmd_u16(handle, SCD30_CMD_SET_TEMPERATURE_OFFSET, offset_centi_c);
}

int scd30_set_altitude(scd30_handle_t *handle, uint16_t altitude_m)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return scd30_send_cmd_u16(handle, SCD30_CMD_SET_ALTITUDE, altitude_m);
}

int scd30_set_forced_recalibration(scd30_handle_t *handle, uint16_t reference_ppm)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return scd30_send_cmd_u16(handle, SCD30_CMD_SET_FORCED_RECAL, reference_ppm);
}

int scd30_get_firmware_version(scd30_handle_t *handle, uint16_t *version)
{
    uint8_t raw[3];
    int ret;

    if (handle == NULL || version == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    ret = scd30_send_cmd(handle, SCD30_CMD_GET_FIRMWARE_VERSION);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(2U);
    ret = scd30_read_words(handle, raw, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *version = (uint16_t)(((uint16_t)raw[0] << 8) | raw[1]);
    return DRIVER_OK;
}

int scd30_read(scd30_handle_t *handle, scd30_data_t *data)
{
    uint8_t ready[3];
    uint8_t raw[SCD30_MEASUREMENT_BYTES];
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = scd30_send_cmd(handle, SCD30_CMD_GET_DATA_READY);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(2U);
    ret = scd30_read_words(handle, ready, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if ((((uint16_t)ready[0] << 8) | ready[1]) == 0U) {
        return (int)IOT_NOT_READY;
    }

    ret = scd30_send_cmd(handle, SCD30_CMD_READ_MEASUREMENT);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(2U);
    ret = scd30_read_words(handle, raw, 6U);
    if (ret != DRIVER_OK) {
        return ret;
    }

    data->co2_ppm = scd30_be_float(&raw[0]);
    data->temperature = scd30_be_float(&raw[6]);
    data->humidity = scd30_be_float(&raw[12]);
    return DRIVER_OK;
}
