#include "acs712.h"
#include "acs712_register.h"
#include "log.h"

static int acs712_check(const acs712_handle_t *handle)
{
    if (handle == NULL || handle->adc == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return (int)IOT_NOT_READY;
    }
    return DRIVER_OK;
}

int acs712_init(acs712_handle_t *handle, iot_adc_t *adc, uint8_t channel)
{
    iot_adc_channel_config_t cfg;
    uint32_t adc_max;

    if (handle == NULL || adc == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    cfg.channel = channel;
    cfg.rank = 1U;
    cfg.sample_time = 0U;   /* 0：由平台选择默认采样时间 */
    if (iot_adc_config_channel(adc, &cfg) != IOT_OK) {
        LOG_E("acs712", "adc channel %u config failed", (unsigned)channel);
        return (int)IOT_IO_ERROR;
    }

    adc_max = iot_adc_get_max_value(adc);

    handle->adc = adc;
    handle->channel = channel;
    handle->vref_mv = ACS712_DEFAULT_VREF_MV;
    handle->adc_max = (adc_max != 0U) ? adc_max : ACS712_DEFAULT_ADC_MAX;
    handle->sens_mv_per_a = ACS712_DEFAULT_SENS_MV_PER_A;
    handle->zero_mv = ACS712_DEFAULT_ZERO_MV;
    handle->initialized = true;

    LOG_I("acs712", "init ok, ch=%u, sens=%.0fmV/A, zero=%.0fmV",
          (unsigned)channel, (double)handle->sens_mv_per_a, (double)handle->zero_mv);
    return DRIVER_OK;
}

int acs712_deinit(acs712_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->adc = NULL;
    handle->initialized = false;
    return DRIVER_OK;
}

int acs712_read_raw(acs712_handle_t *handle, uint32_t *raw)
{
    int ret = acs712_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (raw == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (iot_adc_read(handle->adc, handle->channel, raw, ACS712_ADC_TIMEOUT_MS) != IOT_OK) {
        LOG_E("acs712", "adc read failed");
        return (int)IOT_IO_ERROR;
    }
    return DRIVER_OK;
}

int acs712_read(acs712_handle_t *handle, float *amps)
{
    uint32_t raw = 0U;
    float mv;
    float current;
    int ret;

    if (amps == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    ret = acs712_read_raw(handle, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (raw == 0U || raw >= handle->adc_max) {
        /* 输出贴地或贴顶：常见于超量程、未接线、ADC 基准不匹配 */
        LOG_W("acs712", "adc saturated (raw=%lu)", (unsigned long)raw);
    }

    mv = (float)raw * handle->vref_mv / (float)handle->adc_max;
    current = (mv - handle->zero_mv) / handle->sens_mv_per_a;

    if (current > ACS712_CURRENT_LIMIT_A || current < -ACS712_CURRENT_LIMIT_A) {
        LOG_W("acs712", "current %.1fA out of range, check vref/divider", (double)current);
        current = DRIVER_CLAMP(current, -ACS712_CURRENT_LIMIT_A, ACS712_CURRENT_LIMIT_A);
    }

    *amps = current;
    return DRIVER_OK;
}

int acs712_set_calibration(acs712_handle_t *handle, float sens_mv_per_a, float zero_mv)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if (sens_mv_per_a <= 0.0f || zero_mv < 0.0f) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->sens_mv_per_a = sens_mv_per_a;
    handle->zero_mv = zero_mv;
    return DRIVER_OK;
}

int acs712_set_reference(acs712_handle_t *handle, float vref_mv, uint32_t adc_max)
{
    if (handle == NULL || vref_mv <= 0.0f || adc_max == 0U) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->vref_mv = vref_mv;
    handle->adc_max = adc_max;
    return DRIVER_OK;
}
