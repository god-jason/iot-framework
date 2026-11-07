#include "ntc.h"
#include "ntc_register.h"
#include "log.h"

#include <math.h>

/* 由 ADC 码值推算 NTC 电阻；同时处理短路与开路两种边界 */
static int ntc_compute_resistance(ntc_handle_t *handle, float *ohms)
{
    uint32_t raw = 0U;
    float v;
    float vref = handle->vref_mv;

    if (iot_adc_read(handle->adc, handle->channel, &raw, 0U) != IOT_OK) {
        LOG_E("ntc", "adc read failed");
        return (int)IOT_IO_ERROR;
    }

    if (raw == 0U) {
        /* 电压为 0：NTC 短路到地或通道未接 */
        LOG_E("ntc", "zero voltage, check wiring");
        return (int)IOT_HW_ERROR;
    }

    v = (float)raw * vref / (float)handle->adc_max;

    if (v >= (vref - NTC_VREF_EPS_MV)) {
        /* 满量程：NTC 开路（或上拉缺失），按极大电阻处理并交由上层判定 */
        LOG_E("ntc", "open circuit detected");
        *ohms = NTC_OPEN_RESISTANCE;
        return (int)IOT_HW_ERROR;
    }

    /* 下臂接法：R_ntc = r_series * V / (VCC - V) */
    *ohms = (float)handle->r_series * v / (vref - v);
    if (*ohms <= 0.0f) {
        return (int)IOT_HW_ERROR;
    }
    return DRIVER_OK;
}

static float ntc_resistance_to_celsius(const ntc_handle_t *handle, float ohms)
{
    float inv_t;

    /* 1/T = 1/T25 + ln(R/R25)/B */
    inv_t = (1.0f / NTC_T25_K) + logf(ohms / handle->r25) / handle->beta;
    if (fabsf(inv_t) < 1.0e-9f) {
        return NTC_TEMP_MAX_C;   /* 防止除零，理论上不可达 */
    }
    return (1.0f / inv_t) - NTC_KELVIN_OFFSET;
}

int ntc_read_resistance(ntc_handle_t *handle, float *ohms)
{
    if (handle == NULL || ohms == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return ntc_compute_resistance(handle, ohms);
}

int ntc_init(ntc_handle_t *handle, iot_adc_t *adc, uint8_t channel,
             uint32_t r_series, float r25, float beta,
             float vref_mv, uint32_t adc_max)
{
    if (handle == NULL || adc == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (r_series == 0U || r25 <= 0.0f || beta <= 0.0f ||
        vref_mv <= 0.0f || adc_max == 0U) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->adc = adc;
    handle->channel = channel;
    handle->r_series = r_series;
    handle->r25 = r25;
    handle->beta = beta;
    handle->vref_mv = vref_mv;
    handle->adc_max = adc_max;
    handle->initialized = true;

    LOG_I("ntc", "init ok, ch=%u, B=%.0f, R25=%.0f",
          (unsigned int)channel, (double)beta, (double)r25);
    return DRIVER_OK;
}

int ntc_deinit(ntc_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->adc = NULL;
    handle->initialized = false;
    return DRIVER_OK;
}

int ntc_read(ntc_handle_t *handle, float *temperature)
{
    float ohms = 0.0f;
    float celsius;
    int ret;

    if (handle == NULL || temperature == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = ntc_compute_resistance(handle, &ohms);
    if (ret != DRIVER_OK) {
        /* 开路 / 短路属于可诊断的硬件异常，不输出伪温度值 */
        return ret;
    }

    celsius = ntc_resistance_to_celsius(handle, ohms);
    if (celsius < NTC_TEMP_MIN_C || celsius > NTC_TEMP_MAX_C) {
        LOG_W("ntc", "temperature %.1f out of range", (double)celsius);
        celsius = DRIVER_CLAMP(celsius, NTC_TEMP_MIN_C, NTC_TEMP_MAX_C);
    }

    *temperature = celsius;
    return DRIVER_OK;
}
