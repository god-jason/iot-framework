#include "ldr.h"
#include "ldr_register.h"
#include "ros.h"
#include "common.h"

static int ldr_check(const ldr_handle_t *handle)
{
    if (handle == NULL || handle->adc == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return (int)HAL_NOT_READY;
    }
    return HAL_OK;
}

int ldr_init(ldr_handle_t *handle, hal_adc_t *adc, uint8_t channel)
{
    hal_adc_channel_config_t cfg;
    uint32_t adc_max;

    if (handle == NULL || adc == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    cfg.channel = channel;
    cfg.rank = 1U;
    cfg.sample_time = 0U;   /* 0：由平台选择默认采样时间 */
    if (hal_adc_config_channel(adc, &cfg) != HAL_OK) {
        LOG_E("ldr", "adc channel %u config failed", (unsigned)channel);
        return (int)HAL_IO_ERROR;
    }

    adc_max = hal_adc_get_max_value(adc);

    handle->adc = adc;
    handle->channel = channel;
    handle->vref_mv = LDR_DEFAULT_VREF_MV;
    handle->adc_max = (adc_max != 0U) ? adc_max : LDR_DEFAULT_ADC_MAX;
    handle->dark_mv = LDR_DEFAULT_DARK_MV;
    handle->bright_mv = LDR_DEFAULT_BRIGHT_MV;
    handle->initialized = true;

    LOG_I("ldr", "init ok, ch=%u, dark=%.0fmV, bright=%.0fmV",
          (unsigned)channel, (double)handle->dark_mv, (double)handle->bright_mv);
    return HAL_OK;
}

int ldr_deinit(ldr_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->adc = NULL;
    handle->initialized = false;
    return HAL_OK;
}

int ldr_read_raw(ldr_handle_t *handle, uint32_t *raw)
{
    int ret = ldr_check(handle);

    if (ret != HAL_OK) {
        return ret;
    }
    if (raw == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (hal_adc_read(handle->adc, handle->channel, raw, LDR_ADC_TIMEOUT_MS) != HAL_OK) {
        LOG_E("ldr", "adc read failed");
        return (int)HAL_IO_ERROR;
    }
    return HAL_OK;
}

int ldr_read(ldr_handle_t *handle, ldr_data_t *data)
{
    float mv;
    float span;
    float value;
    int ret;

    if (data == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    data->raw = 0U;
    data->percent = 0.0f;

    ret = ldr_read_raw(handle, &data->raw);
    if (ret != HAL_OK) {
        return ret;
    }

    span = handle->bright_mv - handle->dark_mv;
    if (span == 0.0f) {
        LOG_E("ldr", "invalid calibration span");
        return (int)HAL_HW_ERROR;
    }

    mv = (float)data->raw * handle->vref_mv / (float)handle->adc_max;
    value = (mv - handle->dark_mv) * 100.0f / span;
    if (value < 0.0f || value > 100.0f) {
        LOG_W("ldr", "mv=%.0f out of calibrated span", (double)mv);
    }

    if (value < 0.0f) {
        value = 0.0f;
    } else if (value > 100.0f) {
        value = 100.0f;
    }
    data->percent = value;
    return HAL_OK;
}

int ldr_set_calibration(ldr_handle_t *handle, float dark_mv, float bright_mv)
{
    float span;

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    span = bright_mv - dark_mv;
    if (span < 0.0f) {
        span = -span;
    }
    if (span < LDR_MIN_SPAN_MV) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->dark_mv = dark_mv;
    handle->bright_mv = bright_mv;
    return HAL_OK;
}

int ldr_set_reference(ldr_handle_t *handle, float vref_mv, uint32_t adc_max)
{
    if (handle == NULL || vref_mv <= 0.0f || adc_max == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->vref_mv = vref_mv;
    handle->adc_max = adc_max;
    return HAL_OK;
}
