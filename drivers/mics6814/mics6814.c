#include "mics6814.h"
#include "mics6814_register.h"
#include "log.h"

/* Rs = RL * (Vc - Vout) / Vout */
static float mics6814_rs_from_mv(uint16_t adc_mv, uint16_t vc_mv, uint16_t rl_ohm)
{
    if (adc_mv == 0U || adc_mv >= vc_mv || rl_ohm == 0U) {
        return 0.0f;
    }
    return (float)rl_ohm * ((float)vc_mv - (float)adc_mv) / (float)adc_mv;
}

static int mics6814_check(const mics6814_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return DRIVER_OK;
}

static bool mics6814_ch_valid(mics6814_channel_e ch)
{
    return ((int)ch >= 0 && (int)ch < (int)MICS6814_CH_MAX) ? true : false;
}

int mics6814_init(mics6814_handle_t *handle, uint16_t vc_mv, uint16_t rl_ohm)
{
    uint8_t i;

    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->vc_mv = (vc_mv != 0U) ? vc_mv : MICS6814_DEFAULT_VC_MV;
    rl_ohm = (rl_ohm != 0U) ? rl_ohm : MICS6814_DEFAULT_RL_OHM;
    for (i = 0U; i < MICS6814_CH_COUNT; i++) {
        handle->rl_ohm[i] = rl_ohm;
        handle->r0_ohm[i] = 0U;
        handle->ppm_scale[i] = 0.0f;
    }
    handle->initialized = true;
    LOG_I("mics6814", "init ok, vc=%umV rl=%uohm",
          (unsigned)handle->vc_mv, (unsigned)rl_ohm);
    return DRIVER_OK;
}

int mics6814_deinit(mics6814_handle_t *handle)
{
    uint8_t i;

    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    for (i = 0U; i < MICS6814_CH_COUNT; i++) {
        handle->r0_ohm[i] = 0U;
        handle->ppm_scale[i] = 0.0f;
    }
    handle->initialized = false;
    return DRIVER_OK;
}

int mics6814_set_rl(mics6814_handle_t *handle, mics6814_channel_e ch, uint16_t rl_ohm)
{
    int ret = mics6814_check(handle);

    if (ret != DRIVER_OK || !mics6814_ch_valid(ch) || rl_ohm == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->rl_ohm[ch] = rl_ohm;
    return DRIVER_OK;
}

int mics6814_calibrate_r0(mics6814_handle_t *handle, mics6814_channel_e ch,
                          uint16_t adc_mv, float clean_air_ratio)
{
    float rs;
    int ret = mics6814_check(handle);

    if (ret != DRIVER_OK || !mics6814_ch_valid(ch)) {
        return (int)HAL_INVALID_PARAM;
    }
    if (clean_air_ratio <= 0.0f || adc_mv < MICS6814_MIN_VALID_MV ||
        adc_mv >= handle->vc_mv) {
        return (int)HAL_INVALID_PARAM;
    }
    rs = mics6814_rs_from_mv(adc_mv, handle->vc_mv, handle->rl_ohm[ch]);
    if (rs <= 0.0f) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->r0_ohm[ch] = (uint32_t)(rs / clean_air_ratio + 0.5f);
    if (handle->r0_ohm[ch] == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    LOG_I("mics6814", "ch%u R0=%luohm", (unsigned)ch,
          (unsigned long)handle->r0_ohm[ch]);
    return DRIVER_OK;
}

int mics6814_set_ppm_scale(mics6814_handle_t *handle, mics6814_channel_e ch,
                           float ppm_scale)
{
    int ret = mics6814_check(handle);

    if (ret != DRIVER_OK || !mics6814_ch_valid(ch) || ppm_scale < 0.0f) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->ppm_scale[ch] = ppm_scale;
    return DRIVER_OK;
}

int mics6814_read(mics6814_handle_t *handle, const uint16_t *adc_mv,
                  mics6814_data_t *data)
{
    uint8_t i;
    int ret = mics6814_check(handle);

    if (ret != DRIVER_OK || adc_mv == NULL || data == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    for (i = 0U; i < MICS6814_CH_COUNT; i++) {
        data->mv[i] = adc_mv[i];
        data->rs_ohm[i] = 0.0f;
        data->ratio[i] = 0.0f;
        data->ppm_est[i] = 0.0f;
        data->ppm_valid[i] = false;

        if (adc_mv[i] < MICS6814_MIN_VALID_MV || adc_mv[i] >= handle->vc_mv) {
            LOG_W("mics6814", "ch%u adc %umV out of range", (unsigned)i,
                  (unsigned)adc_mv[i]);
            continue;
        }
        data->rs_ohm[i] = mics6814_rs_from_mv(adc_mv[i], handle->vc_mv,
                                              handle->rl_ohm[i]);
        if (handle->r0_ohm[i] == 0U) {
            continue;   /* 未标定：只输出 mv 与 Rs */
        }
        data->ratio[i] = data->rs_ohm[i] / (float)handle->r0_ohm[i];
        if (handle->ppm_scale[i] > 0.0f) {
            /* 线性近似，需按 datasheet 曲线标定 */
            data->ppm_est[i] = handle->ppm_scale[i] * data->ratio[i];
            data->ppm_valid[i] = true;
        }
    }
    return DRIVER_OK;
}
