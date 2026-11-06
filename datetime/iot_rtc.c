#include "iot_rtc.h"
#include "iot_datetime.h"

static const iot_rtc_ops_t *s_ops = NULL;

int iot_rtc_register(const iot_rtc_ops_t *ops)
{
    if (ops == NULL) {
        return IOT_ERR_PARAM;
    }
    s_ops = ops;
    if (ops->init != NULL) {
        ops->init(ops->ctx);
    }
    return IOT_OK;
}

int iot_rtc_get(iot_datetime_t *dt)
{
    if (dt == NULL) {
        return IOT_ERR_PARAM;
    }
    if (s_ops != NULL && s_ops->get != NULL &&
        s_ops->get(s_ops->ctx, dt) == IOT_OK) {
        return IOT_OK;
    }
    /* 无硬件 RTC：回退软件时钟 */
    return iot_datetime_get(dt);
}

int iot_rtc_set(const iot_datetime_t *dt)
{
    if (dt == NULL) {
        return IOT_ERR_PARAM;
    }
    if (s_ops != NULL && s_ops->set != NULL &&
        s_ops->set(s_ops->ctx, dt) == IOT_OK) {
        return IOT_OK;
    }
    return iot_datetime_set(dt);
}