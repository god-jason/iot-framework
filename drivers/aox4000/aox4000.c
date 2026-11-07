#include "aox4000.h"
#include "aox4000_register.h"
#include "log.h"

int aox4000_init(aox4000_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->initialized = true;
    LOG_I("aox4000", "init ok");
    return DRIVER_OK;
}

int aox4000_deinit(aox4000_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->initialized = false;
    return DRIVER_OK;
}

/* 校验：对帧头到校验字节之前的所有字节求和，取反加一 */
static uint8_t aox4000_checksum(const uint8_t *buffer, size_t length)
{
    uint8_t sum = 0U;
    size_t i;

    for (i = 0U; i < length; i++) {
        sum = (uint8_t)(sum + buffer[i]);
    }
    return (uint8_t)((uint8_t)(~sum) + 1U);
}

int aox4000_parse(aox4000_handle_t *handle, const uint8_t *buffer, size_t length,
                  aox4000_data_t *data)
{
    uint8_t expect;
    uint16_t raw;
    float percent;

    if (handle == NULL || !handle->initialized || buffer == NULL || data == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (length < AOX4000_FRAME_LEN) {
        return (int)PORT_INVALID_PARAM;
    }
    if (buffer[0] != AOX4000_HEAD0 || buffer[1] != AOX4000_HEAD1) {
        LOG_D("aox4000", "bad header");
        return (int)PORT_CRC_ERROR;
    }

    expect = aox4000_checksum(buffer, AOX4000_OFF_CHECKSUM);
    if (expect != buffer[AOX4000_OFF_CHECKSUM]) {
        LOG_D("aox4000", "checksum mismatch");
        return (int)PORT_CRC_ERROR;
    }

    raw = (uint16_t)(((uint16_t)buffer[AOX4000_OFF_O2_H] << 8) |
                     (uint16_t)buffer[AOX4000_OFF_O2_L]);
    percent = (float)raw * AOX4000_O2_LSB_PERCENT;
    if (percent < AOX4000_O2_MIN_PERCENT || percent > AOX4000_O2_MAX_PERCENT) {
        LOG_W("aox4000", "o2 %.2f%% out of range", (double)percent);
        return (int)PORT_INVALID_PARAM;
    }

    data->raw = raw;
    data->o2_percent = percent;
    data->checksum_ok = true;
    return DRIVER_OK;
}
