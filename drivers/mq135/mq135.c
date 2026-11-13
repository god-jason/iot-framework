#include "mq135.h"
#include "mq135_register.h"
#include "ros.h"
#include "common.h"

/* Rs = RL * (Vc - Vout) / Vout */
static float mq135_rs_from_mv(uint16_t adc_mv, uint16_t vc_mv, uint16_t rl_ohm)
{
    if (adc_mv == 0U || adc_mv >= vc_mv) {
        return 0.0f;
    }
    return (float)rl_ohm * ((float)vc_mv - (float)adc_mv) / (float)adc_mv;
}

static int mq135_check(const mq135_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return HAL_OK;
}

int mq135_init(mq135_handle_t *handle, uint16_t vc_mv, uint16_t rl_ohm)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->vc_mv = (vc_mv != 0U) ? vc_mv : MQ135_DEFAULT_VC_MV;
    handle->rl_ohm = (rl_ohm != 0U) ? rl_ohm : MQ135_DEFAULT_RL_OHM;
    handle->r0_ohm = 0U;
    handle->ppm_scale = 0.0f;
    handle->initialized = true;
    LOG_I("mq135", "init ok, vc=%umV rl=%uohm",
          (unsigned)handle->vc_mv, (unsigned)handle->rl_ohm);
    return HAL_OK;
}

int mq135_deinit(mq135_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->r0_ohm = 0U;
    handle->ppm_scale = 0.0f;
    return HAL_OK;
}

int mq135_calibrate_r0(mq135_handle_t *handle, uint16_t adc_mv, float clean_air_ratio)
{
    float rs;
    int ret = mq135_check(handle);

    if (ret != HAL_OK) {
        return ret;
    }
    if (clean_air_ratio <= 0.0f || adc_mv < MQ135_MIN_VALID_MV || adc_mv >= handle->vc_mv) {
        return (int)HAL_INVALID_PARAM;
    }
    rs = mq135_rs_from_mv(adc_mv, handle->vc_mv, handle->rl_ohm);
    if (rs <= 0.0f) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->r0_ohm = (uint32_t)(rs / clean_air_ratio + 0.5f);
    if (handle->r0_ohm == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    LOG_I("mq135", "R0 calibrated: %luohm", (unsigned long)handle->r0_ohm);
    return HAL_OK;
}

int mq135_set_ppm_scale(mq135_handle_t *handle, float ppm_scale)
{
    int ret = mq135_check(handle);

    if (ret != HAL_OK) {
        return ret;
    }
    if (ppm_scale < 0.0f) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->ppm_scale = ppm_scale;
    return HAL_OK;
}

int mq135_read(mq135_handle_t *handle, uint16_t adc_mv, mq135_data_t *data)
{
    int ret = mq135_check(handle);

    if (ret != HAL_OK || data == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    data->mv = adc_mv;
    data->rs_ohm = 0.0f;
    data->ratio = 0.0f;
    data->ppm_est = 0.0f;
    data->ppm_valid = false;

    if (adc_mv < MQ135_MIN_VALID_MV || adc_mv >= handle->vc_mv) {
        LOG_W("mq135", "adc %umV out of range", (unsigned)adc_mv);
        return (int)HAL_INVALID_PARAM;
    }
    data->rs_ohm = mq135_rs_from_mv(adc_mv, handle->vc_mv, handle->rl_ohm);
    if (handle->r0_ohm == 0U) {
        return HAL_OK;   /* 未标定 R0：只输出 mv 与 Rs */
    }
    data->ratio = data->rs_ohm / (float)handle->r0_ohm;
    if (handle->ppm_scale > 0.0f) {
        data->ppm_est = handle->ppm_scale * data->ratio;   /* 线性近似，需标定 */
        data->ppm_valid = true;
    }
    return HAL_OK;
}
