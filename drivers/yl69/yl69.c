#include "yl69.h"
#include "yl69_register.h"
#include "log.h"

static int yl69_check(const yl69_handle_t *handle)
{
    if (handle == NULL || handle->adc == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return (int)HAL_NOT_READY;
    }
    return DRIVER_OK;
}

int yl69_init(yl69_handle_t *handle, hal_adc_t *adc, uint8_t channel)
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
        LOG_E("yl69", "adc channel %u config failed", (unsigned)channel);
        return (int)HAL_IO_ERROR;
    }

    adc_max = hal_adc_get_max_value(adc);

    handle->adc = adc;
    handle->channel = channel;
    handle->vref_mv = YL69_DEFAULT_VREF_MV;
    handle->adc_max = (adc_max != 0U) ? adc_max : YL69_DEFAULT_ADC_MAX;
    handle->dry_mv = YL69_DEFAULT_DRY_MV;
    handle->wet_mv = YL69_DEFAULT_WET_MV;
    handle->initialized = true;

    LOG_I("yl69", "init ok, ch=%u, dry=%.0fmV, wet=%.0fmV",
          (unsigned)channel, (double)handle->dry_mv, (double)handle->wet_mv);
    return DRIVER_OK;
}

int yl69_deinit(yl69_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->adc = NULL;
    handle->initialized = false;
    return DRIVER_OK;
}

int yl69_read_raw(yl69_handle_t *handle, uint32_t *raw)
{
    int ret = yl69_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (raw == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (hal_adc_read(handle->adc, handle->channel, raw, YL69_ADC_TIMEOUT_MS) != HAL_OK) {
        LOG_E("yl69", "adc read failed");
        return (int)HAL_IO_ERROR;
    }
    return DRIVER_OK;
}

int yl69_read(yl69_handle_t *handle, float *percent)
{
    uint32_t raw = 0U;
    float mv;
    float value;
    int ret;

    if (percent == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    ret = yl69_read_raw(handle, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }

    mv = (float)raw * handle->vref_mv / (float)handle->adc_max;
    value = (handle->dry_mv - mv) * 100.0f / (handle->dry_mv - handle->wet_mv);
    if (value < 0.0f || value > 100.0f) {
        LOG_W("yl69", "mv=%.0f out of calibrated span", (double)mv);
    }

    *percent = DRIVER_CLAMP(value, 0.0f, 100.0f);
    return DRIVER_OK;
}

int yl69_set_calibration(yl69_handle_t *handle, float dry_mv, float wet_mv)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (dry_mv <= wet_mv || (dry_mv - wet_mv) < YL69_MIN_SPAN_MV) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->dry_mv = dry_mv;
    handle->wet_mv = wet_mv;
    return DRIVER_OK;
}

int yl69_set_reference(yl69_handle_t *handle, float vref_mv, uint32_t adc_max)
{
    if (handle == NULL || vref_mv <= 0.0f || adc_max == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->vref_mv = vref_mv;
    handle->adc_max = adc_max;
    return DRIVER_OK;
}
