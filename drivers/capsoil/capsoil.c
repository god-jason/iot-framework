#include "capsoil.h"
#include "capsoil_register.h"
#include "log.h"

static int capsoil_check(const capsoil_handle_t *handle)
{
    if (handle == NULL || handle->adc == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return (int)IOT_NOT_READY;
    }
    return DRIVER_OK;
}

static float capsoil_raw_to_mv(const capsoil_handle_t *handle, uint32_t raw)
{
    return (float)raw * handle->vref_mv / (float)handle->adc_max;
}

int capsoil_init(capsoil_handle_t *handle, iot_adc_t *adc, uint8_t channel)
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
        LOG_E("capsoil", "adc channel %u config failed", (unsigned)channel);
        return (int)IOT_IO_ERROR;
    }

    adc_max = iot_adc_get_max_value(adc);

    handle->adc = adc;
    handle->channel = channel;
    handle->vref_mv = CAPSOIL_DEFAULT_VREF_MV;
    handle->adc_max = (adc_max != 0U) ? adc_max : CAPSOIL_DEFAULT_ADC_MAX;
    handle->dry_mv = CAPSOIL_DEFAULT_DRY_MV;
    handle->wet_mv = CAPSOIL_DEFAULT_WET_MV;
    handle->initialized = true;

    LOG_I("capsoil", "init ok, ch=%u, dry=%.0fmV, wet=%.0fmV",
          (unsigned)channel, (double)handle->dry_mv, (double)handle->wet_mv);
    return DRIVER_OK;
}

int capsoil_deinit(capsoil_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->adc = NULL;
    handle->initialized = false;
    return DRIVER_OK;
}

int capsoil_read_raw(capsoil_handle_t *handle, uint32_t *raw)
{
    int ret = capsoil_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (raw == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (iot_adc_read(handle->adc, handle->channel, raw, CAPSOIL_ADC_TIMEOUT_MS) != IOT_OK) {
        LOG_E("capsoil", "adc read failed");
        return (int)IOT_IO_ERROR;
    }
    return DRIVER_OK;
}

int capsoil_read(capsoil_handle_t *handle, capsoil_data_t *data)
{
    float mv;
    float percent;
    int ret;

    if (data == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    data->raw = 0U;
    data->percent = 0.0f;

    ret = capsoil_read_raw(handle, &data->raw);
    if (ret != DRIVER_OK) {
        return ret;
    }

    mv = capsoil_raw_to_mv(handle, data->raw);
    percent = (handle->dry_mv - mv) * 100.0f / (handle->dry_mv - handle->wet_mv);
    if (percent < 0.0f || percent > 100.0f) {
        LOG_W("capsoil", "mv=%.0f out of calibrated span", (double)mv);
    }
    data->percent = DRIVER_CLAMP(percent, 0.0f, 100.0f);
    return DRIVER_OK;
}

int capsoil_set_calibration(capsoil_handle_t *handle, float dry_mv, float wet_mv)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if (dry_mv <= wet_mv || (dry_mv - wet_mv) < CAPSOIL_MIN_SPAN_MV) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->dry_mv = dry_mv;
    handle->wet_mv = wet_mv;
    return DRIVER_OK;
}

int capsoil_set_reference(capsoil_handle_t *handle, float vref_mv, uint32_t adc_max)
{
    if (handle == NULL || vref_mv <= 0.0f || adc_max == 0U) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->vref_mv = vref_mv;
    handle->adc_max = adc_max;
    return DRIVER_OK;
}
