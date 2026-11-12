#include "mq7.h"
#include "mq7_register.h"
#include "log.h"

/* Rs = RL * (Vc - Vout) / Vout */
static float mq7_rs_from_mv(uint16_t adc_mv, uint16_t vc_mv, uint16_t rl_ohm)
{
    if (adc_mv == 0U || adc_mv >= vc_mv) {
        return 0.0f;
    }
    return (float)rl_ohm * ((float)vc_mv - (float)adc_mv) / (float)adc_mv;
}

static int mq7_check(const mq7_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return HAL_OK;
}

int mq7_init(mq7_handle_t *handle, uint16_t vc_mv, uint16_t rl_ohm)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->vc_mv = (vc_mv != 0U) ? vc_mv : MQ7_DEFAULT_VC_MV;
    handle->rl_ohm = (rl_ohm != 0U) ? rl_ohm : MQ7_DEFAULT_RL_OHM;
    handle->r0_ohm = 0U;
    handle->ppm_scale = 0.0f;
    handle->initialized = true;
    LOG_I("mq7", "init ok, vc=%umV rl=%uohm",
          (unsigned)handle->vc_mv, (unsigned)handle->rl_ohm);
    return HAL_OK;
}

int mq7_deinit(mq7_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->r0_ohm = 0U;
    handle->ppm_scale = 0.0f;
    return HAL_OK;
}

int mq7_calibrate_r0(mq7_handle_t *handle, uint16_t adc_mv, float clean_air_ratio)
{
    float rs;
    int ret = mq7_check(handle);

    if (ret != HAL_OK) {
        return ret;
    }
    if (clean_air_ratio <= 0.0f || adc_mv < MQ7_MIN_VALID_MV || adc_mv >= handle->vc_mv) {
        return (int)HAL_INVALID_PARAM;
    }
    rs = mq7_rs_from_mv(adc_mv, handle->vc_mv, handle->rl_ohm);
    if (rs <= 0.0f) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->r0_ohm = (uint32_t)(rs / clean_air_ratio + 0.5f);
    if (handle->r0_ohm == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    LOG_I("mq7", "R0 calibrated: %luohm", (unsigned long)handle->r0_ohm);
    return HAL_OK;
}

int mq7_set_ppm_scale(mq7_handle_t *handle, float ppm_scale)
{
    int ret = mq7_check(handle);

    if (ret != HAL_OK) {
        return ret;
    }
    if (ppm_scale < 0.0f) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->ppm_scale = ppm_scale;
    return HAL_OK;
}

int mq7_read(mq7_handle_t *handle, uint16_t adc_mv, mq7_data_t *data)
{
    int ret = mq7_check(handle);

    if (ret != HAL_OK || data == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    data->mv = adc_mv;
    data->rs_ohm = 0.0f;
    data->ratio = 0.0f;
    data->ppm_est = 0.0f;
    data->ppm_valid = false;

    if (adc_mv < MQ7_MIN_VALID_MV || adc_mv >= handle->vc_mv) {
        LOG_W("mq7", "adc %umV out of range", (unsigned)adc_mv);
        return (int)HAL_INVALID_PARAM;
    }
    data->rs_ohm = mq7_rs_from_mv(adc_mv, handle->vc_mv, handle->rl_ohm);
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
