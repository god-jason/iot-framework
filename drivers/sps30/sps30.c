#include "sps30.h"
#include "sps30_register.h"
#include "log.h"

#include <string.h>

static uint8_t sps30_crc(const uint8_t *data)
{
    return driver_crc8(data, 2U, SPS30_CRC_POLY, SPS30_CRC_INIT);
}

static int sps30_send_cmd(sps30_handle_t *handle, uint16_t command)
{
    uint8_t buf[3];

    buf[0] = (uint8_t)(command >> 8);
    buf[1] = (uint8_t)(command & 0xFFU);
    buf[2] = sps30_crc(buf);
    return driver_i2c_write(&handle->i2c, buf, sizeof(buf));
}

/* 命令 + 1 个 16 位参数（参数带 CRC） */
static int sps30_send_cmd_u16(sps30_handle_t *handle, uint16_t command, uint16_t value)
{
    uint8_t buf[6];

    buf[0] = (uint8_t)(command >> 8);
    buf[1] = (uint8_t)(command & 0xFFU);
    buf[2] = sps30_crc(buf);
    buf[3] = (uint8_t)(value >> 8);
    buf[4] = (uint8_t)(value & 0xFFU);
    buf[5] = sps30_crc(&buf[3]);
    return driver_i2c_write(&handle->i2c, buf, sizeof(buf));
}

/* 读 words 个数据字（每字 2 字节 + 1 字节 CRC），raw 需 words*3 字节 */
static int sps30_read_words(sps30_handle_t *handle, uint8_t *raw, uint16_t words)
{
    uint16_t i;
    int ret;

    ret = driver_i2c_read(&handle->i2c, raw, (size_t)words * 3U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    for (i = 0U; i < words; i++) {
        if (sps30_crc(&raw[(size_t)i * 3U]) != raw[(size_t)i * 3U + 2U]) {
            LOG_E("sps30", "crc error on word %u", (unsigned)i);
            return (int)PORT_CRC_ERROR;
        }
    }
    return DRIVER_OK;
}

/* 大端 IEEE754 float -> 主机 float（假定小端主机） */
static float sps30_be_float(const uint8_t *p)
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

int sps30_init(sps30_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    uint8_t major = 0U;
    uint8_t minor = 0U;

    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : SPS30_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->initialized = false;
    handle->measuring = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("sps30", "probe failed at 0x%02X", (unsigned)handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }

    /* ID 校验：读固件版本（带 CRC） */
    if (sps30_get_version(handle, &major, &minor) != DRIVER_OK) {
        LOG_E("sps30", "version read failed");
        return (int)PORT_NOT_FOUND;
    }

    handle->initialized = true;
    LOG_I("sps30", "init ok, fw=%u.%u", (unsigned)major, (unsigned)minor);
    return DRIVER_OK;
}

int sps30_deinit(sps30_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (handle->initialized && handle->measuring) {
        (void)sps30_send_cmd(handle, SPS30_CMD_STOP_MEASUREMENT);
    }
    handle->initialized = false;
    handle->measuring = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int sps30_start(sps30_handle_t *handle)
{
    uint16_t format = (uint16_t)(((uint16_t)SPS30_OUTPUT_FLOAT_HI << 8) | SPS30_OUTPUT_FLOAT_LO);
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = sps30_send_cmd_u16(handle, SPS30_CMD_START_MEASUREMENT, format);
    if (ret == DRIVER_OK) {
        handle->measuring = true;
        driver_delay_ms(SPS30_START_DELAY_MS);
    }
    return ret;
}

int sps30_stop(sps30_handle_t *handle)
{
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = sps30_send_cmd(handle, SPS30_CMD_STOP_MEASUREMENT);
    if (ret == DRIVER_OK) {
        handle->measuring = false;
    }
    return ret;
}

int sps30_start_fan_cleaning(sps30_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return sps30_send_cmd(handle, SPS30_CMD_START_FAN_CLEAN);
}

int sps30_get_version(sps30_handle_t *handle, uint8_t *major, uint8_t *minor)
{
    uint8_t raw[3];
    int ret;

    if (handle == NULL || major == NULL || minor == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = sps30_send_cmd(handle, SPS30_CMD_READ_VERSION);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(2U);
    ret = sps30_read_words(handle, raw, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *major = raw[0];
    *minor = raw[1];
    return DRIVER_OK;
}

int sps30_get_serial(sps30_handle_t *handle, char *buf, size_t buf_len)
{
    uint8_t raw[48];
    size_t i;
    size_t out = 0U;
    int ret;

    if (handle == NULL || buf == NULL || buf_len == 0U) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = sps30_send_cmd(handle, SPS30_CMD_READ_SERIAL);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(5U);
    ret = sps30_read_words(handle, raw, 16U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    /* 序列号以 ASCII 存放在每个数据字的 2 个数据字节中 */
    for (i = 0U; i < 16U && out + 2U < buf_len; i++) {
        buf[out++] = (char)raw[(size_t)i * 3U];
        buf[out++] = (char)raw[(size_t)i * 3U + 1U];
    }
    buf[out] = '\0';
    return DRIVER_OK;
}

int sps30_read(sps30_handle_t *handle, sps30_data_t *data)
{
    uint8_t ready[3];
    uint8_t raw[SPS30_VALUE_WORDS * 3U];
    uint32_t start;
    uint16_t ready_flag;
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (!handle->measuring) {
        ret = sps30_start(handle);
        if (ret != DRIVER_OK) {
            return ret;
        }
    }

    start = driver_tick_ms();
    for (;;) {
        ret = sps30_send_cmd(handle, SPS30_CMD_DATA_READY);
        if (ret != DRIVER_OK) {
            return ret;
        }
        driver_delay_ms(2U);
        ret = sps30_read_words(handle, ready, 1U);
        if (ret != DRIVER_OK) {
            return ret;
        }
        ready_flag = (uint16_t)(((uint16_t)ready[0] << 8) | ready[1]);
        if ((ready_flag & 0x0001U) != 0U) {
            break;
        }
        if ((uint32_t)(driver_tick_ms() - start) > SPS30_READ_TIMEOUT_MS) {
            return (int)PORT_TIMEOUT;
        }
        driver_delay_ms(SPS30_POLL_DELAY_MS);
    }

    ret = sps30_send_cmd(handle, SPS30_CMD_READ_VALUES);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(2U);
    ret = sps30_read_words(handle, raw, SPS30_VALUE_WORDS);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 每个 float 占 2 个数据字：6 字节（4 数据 + 2 CRC） */
    data->pm1 = sps30_be_float(&raw[0]);
    data->pm25 = sps30_be_float(&raw[6]);
    data->pm4 = sps30_be_float(&raw[12]);
    data->pm10 = sps30_be_float(&raw[18]);
    /* 其后 5 个值为数量浓度，跳过；第 10 个值为典型粒径 */
    data->typical_size = sps30_be_float(&raw[54]);
    return DRIVER_OK;
}
