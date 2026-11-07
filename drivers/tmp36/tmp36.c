#include "tmp36.h"
#include "tmp36_register.h"
#include "log.h"

static int tmp36_check(const tmp36_handle_t *handle)
{
    if (handle == NULL || handle->adc == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return (int)PORT_NOT_READY;
    }
    return DRIVER_OK;
}

int tmp36_init(tmp36_handle_t *handle, port_adc_handle_t *adc, uint8_t channel)
{
    port_adc_channel_config_t cfg;
    uint32_t adc_max;

    if (handle == NULL || adc == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    cfg.channel = channel;
    cfg.rank = 1U;
    cfg.sample_time = 0U;   /* 0：由平台选择默认采样时间 */
    if (port_adc_config_channel(adc, &cfg) != PORT_OK) {
        LOG_E("tmp36", "adc channel %u config failed", (unsigned)channel);
        return (int)PORT_IO_ERROR;
    }

    adc_max = port_adc_get_max_value(adc);

    handle->adc = adc;
    handle->channel = channel;
    handle->vref_mv = TMP36_DEFAULT_VREF_MV;
    handle->adc_max = (adc_max != 0U) ? adc_max : TMP36_DEFAULT_ADC_MAX;
    handle->initialized = true;

    LOG_I("tmp36", "init ok, ch=%u, vref=%.0fmV",
          (unsigned)channel, (double)handle->vref_mv);
    return DRIVER_OK;
}

int tmp36_deinit(tmp36_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->adc = NULL;
    handle->initialized = false;
    return DRIVER_OK;
}

int tmp36_read_raw(tmp36_handle_t *handle, uint32_t *raw)
{
    int ret = tmp36_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (raw == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (port_adc_read(handle->adc, handle->channel, raw, TMP36_ADC_TIMEOUT_MS) != PORT_OK) {
        LOG_E("tmp36", "adc read failed");
        return (int)PORT_IO_ERROR;
    }
    return DRIVER_OK;
}

int tmp36_read(tmp36_handle_t *handle, float *celsius)
{
    uint32_t raw = 0U;
    float mv;
    float temperature;
    int ret;

    if (celsius == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = tmp36_read_raw(handle, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }

    mv = (float)raw * handle->vref_mv / (float)handle->adc_max;
    temperature = (mv - TMP36_ZERO_OFFSET_MV) / TMP36_MV_PER_C;

    if (temperature < TMP36_TEMP_MIN_C || temperature > TMP36_TEMP_MAX_C) {
        LOG_W("tmp36", "temperature %.1fC out of range", (double)temperature);
    }

    *celsius = DRIVER_CLAMP(temperature, TMP36_TEMP_MIN_C, TMP36_TEMP_MAX_C);
    return DRIVER_OK;
}

int tmp36_set_reference(tmp36_handle_t *handle, float vref_mv, uint32_t adc_max)
{
    if (handle == NULL || vref_mv <= 0.0f || adc_max == 0U) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->vref_mv = vref_mv;
    handle->adc_max = adc_max;
    return DRIVER_OK;
}
