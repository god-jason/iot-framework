#include "fc37.h"
#include "fc37_register.h"
#include "log.h"

static int fc37_check(const fc37_handle_t *handle)
{
    if (handle == NULL || handle->adc == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return (int)IOT_NOT_READY;
    }
    return DRIVER_OK;
}

int fc37_init(fc37_handle_t *handle, iot_adc_t *adc, uint8_t channel)
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
        LOG_E("fc37", "adc channel %u config failed", (unsigned)channel);
        return (int)IOT_IO_ERROR;
    }

    adc_max = iot_adc_get_max_value(adc);

    handle->adc = adc;
    handle->channel = channel;
    handle->vref_mv = FC37_DEFAULT_VREF_MV;
    handle->adc_max = (adc_max != 0U) ? adc_max : FC37_DEFAULT_ADC_MAX;
    handle->dry_mv = FC37_DEFAULT_DRY_MV;
    handle->wet_mv = FC37_DEFAULT_WET_MV;
    handle->initialized = true;

    LOG_I("fc37", "init ok, ch=%u, dry=%.0fmV, wet=%.0fmV",
          (unsigned)channel, (double)handle->dry_mv, (double)handle->wet_mv);
    return DRIVER_OK;
}

int fc37_deinit(fc37_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->adc = NULL;
    handle->initialized = false;
    return DRIVER_OK;
}

int fc37_read_raw(fc37_handle_t *handle, uint32_t *raw)
{
    int ret = fc37_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (raw == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (iot_adc_read(handle->adc, handle->channel, raw, FC37_ADC_TIMEOUT_MS) != IOT_OK) {
        LOG_E("fc37", "adc read failed");
        return (int)IOT_IO_ERROR;
    }
    return DRIVER_OK;
}

int fc37_read(fc37_handle_t *handle, float *percent)
{
    uint32_t raw = 0U;
    float mv;
    float value;
    int ret;

    if (percent == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    ret = fc37_read_raw(handle, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }

    mv = (float)raw * handle->vref_mv / (float)handle->adc_max;
    value = (handle->dry_mv - mv) * 100.0f / (handle->dry_mv - handle->wet_mv);
    if (value < 0.0f || value > 100.0f) {
        LOG_W("fc37", "mv=%.0f out of calibrated span", (double)mv);
    }

    *percent = DRIVER_CLAMP(value, 0.0f, 100.0f);
    return DRIVER_OK;
}

int fc37_set_calibration(fc37_handle_t *handle, float dry_mv, float wet_mv)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if (dry_mv <= wet_mv || (dry_mv - wet_mv) < FC37_MIN_SPAN_MV) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->dry_mv = dry_mv;
    handle->wet_mv = wet_mv;
    return DRIVER_OK;
}

int fc37_set_reference(fc37_handle_t *handle, float vref_mv, uint32_t adc_max)
{
    if (handle == NULL || vref_mv <= 0.0f || adc_max == 0U) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->vref_mv = vref_mv;
    handle->adc_max = adc_max;
    return DRIVER_OK;
}
