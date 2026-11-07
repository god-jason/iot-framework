#include "sound_lm393.h"
#include "sound_lm393_register.h"
#include "log.h"

static int sound_lm393_check(const sound_lm393_handle_t *handle)
{
    if (handle == NULL || handle->adc == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return (int)IOT_NOT_READY;
    }
    return DRIVER_OK;
}

int sound_lm393_init(sound_lm393_handle_t *handle, iot_adc_t *adc, uint8_t channel)
{
    iot_adc_channel_config_t cfg;
    uint32_t adc_max;

    if (handle == NULL || adc == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    cfg.channel = channel;
    cfg.rank = 1U;
    cfg.sample_time = 0U;   /* 0：由平台选择默认采样时间（音频宜取较长采样时间） */
    if (iot_adc_config_channel(adc, &cfg) != IOT_OK) {
        LOG_E("sound", "adc channel %u config failed", (unsigned)channel);
        return (int)IOT_IO_ERROR;
    }

    adc_max = iot_adc_get_max_value(adc);

    handle->adc = adc;
    handle->channel = channel;
    handle->vref_mv = SOUND_LM393_DEFAULT_VREF_MV;
    handle->adc_max = (adc_max != 0U) ? adc_max : SOUND_LM393_DEFAULT_ADC_MAX;
    handle->initialized = true;

    LOG_I("sound", "init ok, ch=%u, vref=%.0fmV",
          (unsigned)channel, (double)handle->vref_mv);
    return DRIVER_OK;
}

int sound_lm393_deinit(sound_lm393_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->adc = NULL;
    handle->initialized = false;
    return DRIVER_OK;
}

int sound_lm393_read_raw(sound_lm393_handle_t *handle, uint32_t *raw)
{
    int ret = sound_lm393_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (raw == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (iot_adc_read(handle->adc, handle->channel, raw, SOUND_LM393_ADC_TIMEOUT_MS) != IOT_OK) {
        LOG_E("sound", "adc read failed");
        return (int)IOT_IO_ERROR;
    }
    return DRIVER_OK;
}

int sound_lm393_read(sound_lm393_handle_t *handle, sound_lm393_data_t *data)
{
    float mv;
    float value;
    int ret;

    if (data == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    data->raw = 0U;
    data->percent = 0.0f;

    ret = sound_lm393_read_raw(handle, &data->raw);
    if (ret != DRIVER_OK) {
        return ret;
    }

    mv = (float)data->raw * handle->vref_mv / (float)handle->adc_max;
    value = mv * 100.0f / handle->vref_mv;

    data->percent = DRIVER_CLAMP(value, 0.0f, 100.0f);
    return DRIVER_OK;
}

int sound_lm393_set_reference(sound_lm393_handle_t *handle, float vref_mv, uint32_t adc_max)
{
    if (handle == NULL || vref_mv <= 0.0f || adc_max == 0U) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->vref_mv = vref_mv;
    handle->adc_max = adc_max;
    return DRIVER_OK;
}
