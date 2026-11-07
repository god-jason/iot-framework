#include "flex.h"
#include "flex_register.h"
#include "log.h"

static int flex_check(const flex_handle_t *handle)
{
    if (handle == NULL || handle->adc == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return (int)IOT_NOT_READY;
    }
    return DRIVER_OK;
}

int flex_init(flex_handle_t *handle, iot_adc_t *adc, uint8_t channel)
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
        LOG_E("flex", "adc channel %u config failed", (unsigned)channel);
        return (int)IOT_IO_ERROR;
    }

    adc_max = iot_adc_get_max_value(adc);

    handle->adc = adc;
    handle->channel = channel;
    handle->vref_mv = FLEX_DEFAULT_VREF_MV;
    handle->adc_max = (adc_max != 0U) ? adc_max : FLEX_DEFAULT_ADC_MAX;
    handle->flat_mv = FLEX_DEFAULT_FLAT_MV;
    handle->bent_mv = FLEX_DEFAULT_BENT_MV;
    handle->initialized = true;

    LOG_I("flex", "init ok, ch=%u, flat=%.0fmV, bent=%.0fmV",
          (unsigned)channel, (double)handle->flat_mv, (double)handle->bent_mv);
    return DRIVER_OK;
}

int flex_deinit(flex_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->adc = NULL;
    handle->initialized = false;
    return DRIVER_OK;
}

int flex_read_raw(flex_handle_t *handle, uint32_t *raw)
{
    int ret = flex_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (raw == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (iot_adc_read(handle->adc, handle->channel, raw, FLEX_ADC_TIMEOUT_MS) != IOT_OK) {
        LOG_E("flex", "adc read failed");
        return (int)IOT_IO_ERROR;
    }
    return DRIVER_OK;
}

int flex_read(flex_handle_t *handle, flex_data_t *data)
{
    float mv;
    float span;
    float value;
    int ret;

    if (data == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    data->raw = 0U;
    data->normalized = 0.0f;

    ret = flex_read_raw(handle, &data->raw);
    if (ret != DRIVER_OK) {
        return ret;
    }

    span = handle->bent_mv - handle->flat_mv;
    if (span == 0.0f) {
        LOG_E("flex", "invalid calibration span");
        return (int)IOT_HW_ERROR;
    }

    mv = (float)data->raw * handle->vref_mv / (float)handle->adc_max;
    value = (mv - handle->flat_mv) * 100.0f / span;
    if (value < 0.0f || value > 100.0f) {
        LOG_W("flex", "mv=%.0f out of calibrated span", (double)mv);
    }

    data->normalized = DRIVER_CLAMP(value, 0.0f, 100.0f);
    return DRIVER_OK;
}

int flex_set_calibration(flex_handle_t *handle, float flat_mv, float bent_mv)
{
    float span;

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    span = bent_mv - flat_mv;
    if (span < 0.0f) {
        span = -span;
    }
    if (span < FLEX_MIN_SPAN_MV) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->flat_mv = flat_mv;
    handle->bent_mv = bent_mv;
    return DRIVER_OK;
}

int flex_set_reference(flex_handle_t *handle, float vref_mv, uint32_t adc_max)
{
    if (handle == NULL || vref_mv <= 0.0f || adc_max == 0U) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->vref_mv = vref_mv;
    handle->adc_max = adc_max;
    return DRIVER_OK;
}
