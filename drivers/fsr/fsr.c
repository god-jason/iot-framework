#include "fsr.h"
#include "fsr_register.h"
#include "log.h"

static int fsr_check(const fsr_handle_t *handle)
{
    if (handle == NULL || handle->adc == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return (int)PORT_NOT_READY;
    }
    return DRIVER_OK;
}

int fsr_init(fsr_handle_t *handle, port_adc_handle_t *adc, uint8_t channel)
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
        LOG_E("fsr", "adc channel %u config failed", (unsigned)channel);
        return (int)PORT_IO_ERROR;
    }

    adc_max = port_adc_get_max_value(adc);

    handle->adc = adc;
    handle->channel = channel;
    handle->vref_mv = FSR_DEFAULT_VREF_MV;
    handle->adc_max = (adc_max != 0U) ? adc_max : FSR_DEFAULT_ADC_MAX;
    handle->r_series_ohm = FSR_DEFAULT_R_SERIES_OHM;
    handle->force_scale_g_ohm = FSR_DEFAULT_FORCE_SCALE_G_OHM;
    handle->initialized = true;

    LOG_I("fsr", "init ok, ch=%u, r_series=%luohm",
          (unsigned)channel, (unsigned long)handle->r_series_ohm);
    return DRIVER_OK;
}

int fsr_deinit(fsr_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->adc = NULL;
    handle->initialized = false;
    return DRIVER_OK;
}

int fsr_read_raw(fsr_handle_t *handle, uint32_t *raw)
{
    int ret = fsr_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (raw == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (port_adc_read(handle->adc, handle->channel, raw, FSR_ADC_TIMEOUT_MS) != PORT_OK) {
        LOG_E("fsr", "adc read failed");
        return (int)PORT_IO_ERROR;
    }
    return DRIVER_OK;
}

int fsr_read(fsr_handle_t *handle, fsr_data_t *data)
{
    float mv;
    float ohm;
    int ret;

    if (data == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    data->raw = 0U;
    data->resistance_ohm = FSR_OPEN_RESISTANCE;
    data->pressure_g = 0.0f;

    ret = fsr_read_raw(handle, &data->raw);
    if (ret != DRIVER_OK) {
        return ret;
    }

    mv = (float)data->raw * handle->vref_mv / (float)handle->adc_max;

    if (mv >= (handle->vref_mv - FSR_RAIL_EPS_MV)) {
        /* Vout 贴顶意味着 R_fsr ≈ 0：短路或 ADC 基准异常 */
        LOG_E("fsr", "output at rail, short circuit");
        return (int)PORT_HW_ERROR;
    }
    if (mv <= FSR_MIN_VALID_MV) {
        /* 无压力：R_fsr 极大，按开路处理，不产生除零 */
        return DRIVER_OK;
    }

    /* FSR 在上臂：R_fsr = R_series * (Vref - Vout) / Vout */
    ohm = (float)handle->r_series_ohm * (handle->vref_mv - mv) / mv;
    if (ohm <= 0.0f) {
        LOG_E("fsr", "invalid resistance");
        return (int)PORT_HW_ERROR;
    }

    data->resistance_ohm = ohm;
    data->pressure_g = DRIVER_CLAMP(handle->force_scale_g_ohm / ohm, 0.0f, FSR_PRESSURE_MAX_G);
    return DRIVER_OK;
}

int fsr_set_calibration(fsr_handle_t *handle, uint32_t r_series_ohm, float force_scale_g_ohm)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (r_series_ohm == 0U || force_scale_g_ohm <= 0.0f) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->r_series_ohm = r_series_ohm;
    handle->force_scale_g_ohm = force_scale_g_ohm;
    return DRIVER_OK;
}

int fsr_set_reference(fsr_handle_t *handle, float vref_mv, uint32_t adc_max)
{
    if (handle == NULL || vref_mv <= 0.0f || adc_max == 0U) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->vref_mv = vref_mv;
    handle->adc_max = adc_max;
    return DRIVER_OK;
}
