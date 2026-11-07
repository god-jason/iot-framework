#include "cs100.h"
#include "cs100_register.h"
#include "log.h"

int cs100_init(cs100_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->initialized = true;
    LOG_I("cs100", "init ok");
    return DRIVER_OK;
}

int cs100_deinit(cs100_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->initialized = false;
    return DRIVER_OK;
}

int cs100_parse(cs100_handle_t *handle, const uint8_t *buffer, size_t length,
                cs100_data_t *data)
{
    uint16_t sum = 0U;
    uint16_t expect;
    uint16_t actual;
    uint16_t co2;
    size_t i;

    if (handle == NULL || !handle->initialized || buffer == NULL || data == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (length < CS100_FRAME_LEN) {
        return (int)IOT_INVALID_PARAM;
    }
    if (buffer[0] != CS100_HEAD || buffer[1] != CS100_ADDR) {
        LOG_D("cs100", "bad header");
        return (int)IOT_CRC_ERROR;
    }

    for (i = 0U; i < CS100_CHECKSUM_BYTES; i++) {
        sum = (uint16_t)(sum + buffer[i]);
    }
    expect = (uint16_t)(0x10000UL - (uint32_t)sum);
    expect = (uint16_t)(expect & 0xFFFFU);
    actual = (uint16_t)(((uint16_t)buffer[CS100_CHECKSUM_OFF] << 8) |
                        (uint16_t)buffer[CS100_CHECKSUM_OFF + 1U]);
    if (expect != actual) {
        LOG_D("cs100", "checksum mismatch 0x%04X != 0x%04X",
              (unsigned)expect, (unsigned)actual);
        return (int)IOT_CRC_ERROR;
    }

    co2 = (uint16_t)(((uint16_t)buffer[CS100_OFF_CO2_H] << 8) |
                     (uint16_t)buffer[CS100_OFF_CO2_L]);
    if (co2 > CS100_CO2_MAX_PPM) {
        LOG_W("cs100", "co2 %uppm out of range", (unsigned)co2);
        return (int)IOT_INVALID_PARAM;
    }

    data->co2_ppm = co2;
    return DRIVER_OK;
}
