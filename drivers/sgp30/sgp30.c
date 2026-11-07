#include "sgp30.h"
#include "sgp30_register.h"
#include "log.h"

static uint8_t sgp30_crc(const uint8_t *data)
{
    return driver_crc8(data, 2U, SGP30_CRC_POLY, SGP30_CRC_INIT);
}

/* 写命令：2 字节命令 + 1 字节 CRC */
static int sgp30_write_cmd(sgp30_handle_t *handle, uint16_t command)
{
    uint8_t buf[3];

    buf[0] = (uint8_t)(command >> 8);
    buf[1] = (uint8_t)(command & 0xFFU);
    buf[2] = sgp30_crc(buf);
    return driver_i2c_write(&handle->i2c, buf, sizeof(buf));
}

/* 写命令 + 一个 16 位参数（命令与参数各自带 CRC） */
static int sgp30_write_cmd_u16(sgp30_handle_t *handle, uint16_t command, uint16_t value)
{
    uint8_t buf[6];

    buf[0] = (uint8_t)(command >> 8);
    buf[1] = (uint8_t)(command & 0xFFU);
    buf[2] = sgp30_crc(buf);
    buf[3] = (uint8_t)(value >> 8);
    buf[4] = (uint8_t)(value & 0xFFU);
    buf[5] = sgp30_crc(&buf[3]);
    return driver_i2c_write(&handle->i2c, buf, sizeof(buf));
}

/* 读取 words 个 16 位字，每个字后跟 1 字节 CRC */
static int sgp30_read_words(sgp30_handle_t *handle, uint8_t *raw, uint8_t words)
{
    uint8_t i;
    int ret;

    ret = driver_i2c_read(&handle->i2c, raw, (size_t)words * 3U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    for (i = 0U; i < words; i++) {
        if (sgp30_crc(&raw[(size_t)i * 3U]) != raw[(size_t)i * 3U + 2U]) {
            LOG_E("sgp30", "crc error on word %u", (unsigned)i);
            return (int)IOT_CRC_ERROR;
        }
    }
    return DRIVER_OK;
}

static uint16_t sgp30_word_at(const uint8_t *raw, uint8_t index)
{
    size_t off = (size_t)index * 3U;

    return (uint16_t)(((uint16_t)raw[off] << 8) | raw[off + 1U]);
}

int sgp30_init(sgp30_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint8_t raw[3];
    uint16_t feature;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : SGP30_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("sgp30", "probe failed at 0x%02X", (unsigned)handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    ret = sgp30_write_cmd(handle, SGP30_CMD_SOFT_RESET);
    if (ret != DRIVER_OK) {
        LOG_W("sgp30", "soft reset failed: %d", ret);
    }
    driver_delay_ms(SGP30_RESET_DELAY_MS);

    ret = sgp30_write_cmd(handle, SGP30_CMD_IAQ_INIT);
    if (ret != DRIVER_OK) {
        LOG_E("sgp30", "iaq_init failed: %d", ret);
        return ret;
    }
    driver_delay_ms(SGP30_IAQ_INIT_DELAY_MS);

    /* ID 校验：读特性集版本 */
    ret = sgp30_write_cmd(handle, SGP30_CMD_GET_FEATURE_SET);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(2U);
    ret = sgp30_read_words(handle, raw, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    feature = sgp30_word_at(raw, 0U);
    if (feature != SGP30_FEATURE_SET_A && feature != SGP30_FEATURE_SET_B) {
        LOG_E("sgp30", "unexpected feature set 0x%04X", (unsigned)feature);
        return (int)IOT_NOT_FOUND;
    }

    handle->initialized = true;
    LOG_I("sgp30", "init ok, feature=0x%04X", (unsigned)feature);
    return DRIVER_OK;
}

int sgp30_deinit(sgp30_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int sgp30_read(sgp30_handle_t *handle, sgp30_data_t *data)
{
    uint8_t raw[6];
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = sgp30_write_cmd(handle, SGP30_CMD_MEASURE_IAQ);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(SGP30_MEASURE_DELAY_MS);

    ret = sgp30_read_words(handle, raw, 2U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    data->eco2_ppm = sgp30_word_at(raw, 0U);
    data->tvoc_ppb = sgp30_word_at(raw, 1U);
    return DRIVER_OK;
}

int sgp30_get_baseline(sgp30_handle_t *handle, uint16_t *eco2_base, uint16_t *tvoc_base)
{
    uint8_t raw[6];
    int ret;

    if (handle == NULL || eco2_base == NULL || tvoc_base == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    ret = sgp30_write_cmd(handle, SGP30_CMD_GET_BASELINE);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(2U);
    ret = sgp30_read_words(handle, raw, 2U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *eco2_base = sgp30_word_at(raw, 0U);
    *tvoc_base = sgp30_word_at(raw, 1U);
    return DRIVER_OK;
}

int sgp30_set_baseline(sgp30_handle_t *handle, uint16_t eco2_base, uint16_t tvoc_base)
{
    uint8_t buf[9];

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    /* 一次事务：命令(2)+CRC + eCO2 基线(2)+CRC + TVOC 基线(2)+CRC */
    buf[0] = (uint8_t)(SGP30_CMD_SET_BASELINE >> 8);
    buf[1] = (uint8_t)(SGP30_CMD_SET_BASELINE & 0xFFU);
    buf[2] = sgp30_crc(buf);
    buf[3] = (uint8_t)(eco2_base >> 8);
    buf[4] = (uint8_t)(eco2_base & 0xFFU);
    buf[5] = sgp30_crc(&buf[3]);
    buf[6] = (uint8_t)(tvoc_base >> 8);
    buf[7] = (uint8_t)(tvoc_base & 0xFFU);
    buf[8] = sgp30_crc(&buf[6]);
    return driver_i2c_write(&handle->i2c, buf, sizeof(buf));
}

int sgp30_set_humidity(sgp30_handle_t *handle, uint32_t humidity_mg_m3)
{
    uint16_t value;

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    value = (uint16_t)((humidity_mg_m3 > 0xFFFFU) ? 0xFFFFU : humidity_mg_m3);
    return sgp30_write_cmd_u16(handle, SGP30_CMD_SET_HUMIDITY, value);
}
