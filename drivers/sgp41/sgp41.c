#include "sgp41.h"
#include "sgp41_register.h"
#include "log.h"

static uint8_t sgp41_crc(const uint8_t *data)
{
    return driver_crc8(data, 2U, SGP41_CRC_POLY, SGP41_CRC_INIT);
}

static int sgp41_write_cmd(sgp41_handle_t *handle, uint16_t command)
{
    uint8_t buf[3];

    buf[0] = (uint8_t)(command >> 8);
    buf[1] = (uint8_t)(command & 0xFFU);
    buf[2] = sgp41_crc(buf);
    return driver_i2c_write(&handle->i2c, buf, sizeof(buf));
}

/* 命令 + 两个 16 位参数（每个都有 CRC） */
static int sgp41_write_cmd_2args(sgp41_handle_t *handle, uint16_t command,
                                 uint16_t arg0, uint16_t arg1)
{
    uint8_t buf[9];

    buf[0] = (uint8_t)(command >> 8);
    buf[1] = (uint8_t)(command & 0xFFU);
    buf[2] = sgp41_crc(buf);
    buf[3] = (uint8_t)(arg0 >> 8);
    buf[4] = (uint8_t)(arg0 & 0xFFU);
    buf[5] = sgp41_crc(&buf[3]);
    buf[6] = (uint8_t)(arg1 >> 8);
    buf[7] = (uint8_t)(arg1 & 0xFFU);
    buf[8] = sgp41_crc(&buf[6]);
    return driver_i2c_write(&handle->i2c, buf, sizeof(buf));
}

static int sgp41_read_words(sgp41_handle_t *handle, uint8_t *raw, uint8_t words)
{
    uint8_t i;
    int ret;

    ret = driver_i2c_read(&handle->i2c, raw, (size_t)words * 3U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    for (i = 0U; i < words; i++) {
        if (sgp41_crc(&raw[(size_t)i * 3U]) != raw[(size_t)i * 3U + 2U]) {
            LOG_E("sgp41", "crc error on word %u", (unsigned)i);
            return (int)PORT_CRC_ERROR;
        }
    }
    return DRIVER_OK;
}

static uint16_t sgp41_word_at(const uint8_t *raw, uint8_t index)
{
    size_t off = (size_t)index * 3U;

    return (uint16_t)(((uint16_t)raw[off] << 8) | raw[off + 1U]);
}

int sgp41_init(sgp41_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    uint64_t serial = 0U;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : SGP41_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("sgp41", "probe failed at 0x%02X", (unsigned)handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }

    ret = sgp41_write_cmd(handle, SGP41_CMD_SOFT_RESET);
    if (ret != DRIVER_OK) {
        LOG_W("sgp41", "soft reset failed: %d", ret);
    }
    driver_delay_ms(SGP41_RESET_DELAY_MS);

    /* ID 校验：读序列号并验证 CRC */
    if (sgp41_get_serial(handle, &serial) != DRIVER_OK || serial == 0U) {
        LOG_E("sgp41", "serial read failed");
        return (int)PORT_NOT_FOUND;
    }

    handle->initialized = true;
    LOG_I("sgp41", "init ok, serial=0x%04X%08lX",
          (unsigned)(uint16_t)(serial >> 32), (unsigned long)(serial & 0xFFFFFFFFULL));
    return DRIVER_OK;
}

int sgp41_deinit(sgp41_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int sgp41_self_test(sgp41_handle_t *handle)
{
    uint8_t raw[3];
    uint16_t result;
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = sgp41_write_cmd(handle, SGP41_CMD_SELF_TEST);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(SGP41_SELFTEST_DELAY_MS);
    ret = sgp41_read_words(handle, raw, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    result = sgp41_word_at(raw, 0U);
    return (result == SGP41_SELFTEST_PASS) ? DRIVER_OK : (int)PORT_IO_ERROR;
}

int sgp41_get_serial(sgp41_handle_t *handle, uint64_t *serial)
{
    uint8_t raw[9];
    int ret;

    if (handle == NULL || serial == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = sgp41_write_cmd(handle, SGP41_CMD_GET_SERIAL_ID);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(2U);
    ret = sgp41_read_words(handle, raw, 3U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *serial = ((uint64_t)sgp41_word_at(raw, 0U) << 32) |
              ((uint64_t)sgp41_word_at(raw, 1U) << 16) |
              (uint64_t)sgp41_word_at(raw, 2U);
    return DRIVER_OK;
}

int sgp41_execute_conditioning(sgp41_handle_t *handle, sgp41_data_t *data)
{
    uint8_t raw[6];
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = sgp41_write_cmd(handle, SGP41_CMD_EXECUTE_CONDITIONING);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(SGP41_CONDITIONING_MS);
    ret = sgp41_read_words(handle, raw, 2U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (data != NULL) {
        data->sraw_voc = sgp41_word_at(raw, 0U);
        data->sraw_nox = sgp41_word_at(raw, 1U);
    }
    return DRIVER_OK;
}

int sgp41_measure_raw(sgp41_handle_t *handle, sgp41_data_t *data)
{
    uint8_t raw[6];
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = sgp41_write_cmd(handle, SGP41_CMD_MEASURE_RAW);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(SGP41_MEASURE_DELAY_MS);
    ret = sgp41_read_words(handle, raw, 2U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    data->sraw_voc = sgp41_word_at(raw, 0U);
    data->sraw_nox = sgp41_word_at(raw, 1U);
    return DRIVER_OK;
}

int sgp41_measure_raw_compensated(sgp41_handle_t *handle, uint16_t rh_ticks,
                                  uint16_t t_ticks, sgp41_data_t *data)
{
    uint8_t raw[6];
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = sgp41_write_cmd_2args(handle, SGP41_CMD_MEASURE_RAW_COMP, rh_ticks, t_ticks);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(SGP41_MEASURE_DELAY_MS);
    ret = sgp41_read_words(handle, raw, 2U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    data->sraw_voc = sgp41_word_at(raw, 0U);
    data->sraw_nox = sgp41_word_at(raw, 1U);
    return DRIVER_OK;
}
