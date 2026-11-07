#include "mq131.h"
#include "mq131_register.h"
#include "log.h"

/* Rs = RL * (Vc - Vout) / Vout */
static float mq131_rs_from_mv(uint16_t adc_mv, uint16_t vc_mv, uint16_t rl_ohm)
{
    if (adc_mv == 0U || adc_mv >= vc_mv) {
        return 0.0f;
    }
    return (float)rl_ohm * ((float)vc_mv - (float)adc_mv) / (float)adc_mv;
}

static int mq131_check(const mq131_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return DRIVER_OK;
}

int mq131_init(mq131_handle_t *handle, uint16_t vc_mv, uint16_t rl_ohm)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->vc_mv = (vc_mv != 0U) ? vc_mv : MQ131_DEFAULT_VC_MV;
    handle->rl_ohm = (rl_ohm != 0U) ? rl_ohm : MQ131_DEFAULT_RL_OHM;
    handle->r0_ohm = 0U;
    handle->ppm_scale = 0.0f;
    handle->initialized = true;
    LOG_I("mq131", "init ok, vc=%umV rl=%uohm",
          (unsigned)handle->vc_mv, (unsigned)handle->rl_ohm);
    return DRIVER_OK;
}

int mq131_deinit(mq131_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->r0_ohm = 0U;
    handle->ppm_scale = 0.0f;
    return DRIVER_OK;
}

int mq131_calibrate_r0(mq131_handle_t *handle, uint16_t adc_mv, float clean_air_ratio)
{
    float rs;
    int ret = mq131_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (clean_air_ratio <= 0.0f || adc_mv < MQ131_MIN_VALID_MV || adc_mv >= handle->vc_mv) {
        return (int)PORT_INVALID_PARAM;
    }
    rs = mq131_rs_from_mv(adc_mv, handle->vc_mv, handle->rl_ohm);
    if (rs <= 0.0f) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->r0_ohm = (uint32_t)(rs / clean_air_ratio + 0.5f);
    if (handle->r0_ohm == 0U) {
        return (int)PORT_INVALID_PARAM;
    }
    LOG_I("mq131", "R0 calibrated: %luohm", (unsigned long)handle->r0_ohm);
    return DRIVER_OK;
}

int mq131_set_ppm_scale(mq131_handle_t *handle, float ppm_scale)
{
    int ret = mq131_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (ppm_scale < 0.0f) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->ppm_scale = ppm_scale;
    return DRIVER_OK;
}

int mq131_read(mq131_handle_t *handle, uint16_t adc_mv, mq131_data_t *data)
{
    int ret = mq131_check(handle);

    if (ret != DRIVER_OK || data == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    data->mv = adc_mv;
    data->rs_ohm = 0.0f;
    data->ratio = 0.0f;
    data->ppm_est = 0.0f;
    data->ppm_valid = false;

    if (adc_mv < MQ131_MIN_VALID_MV || adc_mv >= handle->vc_mv) {
        LOG_W("mq131", "adc %umV out of range", (unsigned)adc_mv);
        return (int)PORT_INVALID_PARAM;
    }
    data->rs_ohm = mq131_rs_from_mv(adc_mv, handle->vc_mv, handle->rl_ohm);
    if (handle->r0_ohm == 0U) {
        return DRIVER_OK;   /* 未标定 R0：只输出 mv 与 Rs */
    }
    data->ratio = data->rs_ohm / (float)handle->r0_ohm;
    if (handle->ppm_scale > 0.0f) {
        data->ppm_est = handle->ppm_scale * data->ratio;   /* 线性近似，需标定 */
        data->ppm_valid = true;
    }
    return DRIVER_OK;
}
