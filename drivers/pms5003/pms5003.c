#include "pms5003.h"
#include "pms5003_register.h"
#include "log.h"

static uint16_t pms5003_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

int pms5003_init(pms5003_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->initialized = true;
    LOG_I("pms5003", "init ok");
    return HAL_OK;
}

int pms5003_deinit(pms5003_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->initialized = false;
    return HAL_OK;
}

int pms5003_parse(pms5003_handle_t *handle, const uint8_t *buffer, size_t length,
                  pms5003_data_t *data)
{
    uint16_t sum = 0U;
    uint16_t frame_len;
    uint16_t checksum;
    size_t i;

    if (handle == NULL || !handle->initialized || buffer == NULL || data == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (length < PMS5003_FRAME_LEN) {
        return (int)HAL_INVALID_PARAM;
    }
    if (buffer[0] != PMS5003_HEAD0 || buffer[1] != PMS5003_HEAD1) {
        LOG_D("pms5003", "bad header");
        return (int)HAL_CRC_ERROR;
    }

    frame_len = pms5003_u16(&buffer[2]);
    if (frame_len != PMS5003_PAYLOAD_LEN) {
        LOG_D("pms5003", "unexpected length %u", (unsigned)frame_len);
        return (int)HAL_INVALID_PARAM;
    }

    for (i = 0U; i < PMS5003_CHECKSUM_INDEX; i++) {
        sum = (uint16_t)(sum + buffer[i]);
    }
    checksum = pms5003_u16(&buffer[PMS5003_CHECKSUM_INDEX]);
    if (sum != checksum) {
        LOG_D("pms5003", "checksum mismatch: %u != %u", (unsigned)sum, (unsigned)checksum);
        return (int)HAL_CRC_ERROR;
    }

    data->pm1_atm = pms5003_u16(&buffer[PMS5003_OFF_PM1_ATM]);
    data->pm25_atm = pms5003_u16(&buffer[PMS5003_OFF_PM25_ATM]);
    data->pm10_atm = pms5003_u16(&buffer[PMS5003_OFF_PM10_ATM]);
    data->pm1_cf1 = pms5003_u16(&buffer[PMS5003_OFF_PM1_CF1]);
    data->pm25_cf1 = pms5003_u16(&buffer[PMS5003_OFF_PM25_CF1]);
    data->pm10_cf1 = pms5003_u16(&buffer[PMS5003_OFF_PM10_CF1]);
    data->nc_03 = pms5003_u16(&buffer[PMS5003_OFF_NC_03]);
    data->nc_05 = pms5003_u16(&buffer[PMS5003_OFF_NC_05]);
    data->nc_10 = pms5003_u16(&buffer[PMS5003_OFF_NC_10]);
    data->nc_25 = pms5003_u16(&buffer[PMS5003_OFF_NC_25]);
    data->nc_50 = pms5003_u16(&buffer[PMS5003_OFF_NC_50]);
    data->nc_100 = pms5003_u16(&buffer[PMS5003_OFF_NC_100]);
    data->version = pms5003_u16(&buffer[PMS5003_OFF_VERSION]);
    return HAL_OK;
}
