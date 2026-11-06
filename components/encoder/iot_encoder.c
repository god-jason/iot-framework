#include "iot_encoder.h"
#include "iot_logger.h"

int iot_encoder_init(iot_encoder_t *enc, const iot_encoder_ops_t *ops)
{
    if (enc == NULL || ops == NULL || ops->read_count == NULL) {
        return IOT_ERR_PARAM;
    }
    enc->ops = ops;
    enc->count = 0;
    enc->last_count = 0;
    enc->speed = 0.0f;
    return IOT_OK;
}

int iot_encoder_read(iot_encoder_t *enc, int32_t *count)
{
    if (enc == NULL || count == NULL) {
        return IOT_ERR_PARAM;
    }
    if (enc->ops->read_count(enc->ops->ctx, &enc->count) != IOT_OK) {
        LOG_E("encoder", "read fail");   /* read count failed */
        return IOT_ERR_IO;
    }
    *count = enc->count;
    return IOT_OK;
}

int iot_encoder_get_speed(iot_encoder_t *enc, float dt, float *speed)
{
    if (enc == NULL || speed == NULL || dt <= 0.0f) {
        return IOT_ERR_PARAM;
    }
    if (iot_encoder_read(enc, &enc->count) != IOT_OK) {
        return IOT_ERR_IO;
    }
    enc->speed = (float)(enc->count - enc->last_count) / dt;
    enc->last_count = enc->count;
    *speed = enc->speed;
    return IOT_OK;
}

int iot_encoder_reset(iot_encoder_t *enc)
{
    if (enc == NULL) {
        return IOT_ERR_PARAM;
    }
    enc->last_count = enc->count;
    if (enc->ops->reset != NULL) {
        enc->ops->reset(enc->ops->ctx);
    }
    return IOT_OK;
}