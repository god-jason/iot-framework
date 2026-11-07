#include "encoder.h"
#include "iot_logger.h"

int encoder_init(encoder_t *enc, const encoder_ops_t *ops)
{
    if (enc == NULL || ops == NULL || ops->read_count == NULL) {
        return -1;
    }
    enc->ops = ops;
    enc->count = 0;
    enc->last_count = 0;
    enc->speed = 0.0f;
    return IOT_OK;
}

int encoder_read(encoder_t *enc, int32_t *count)
{
    if (enc == NULL || count == NULL) {
        return -1;
    }
    if (enc->ops->read_count(enc->ops->ctx, &enc->count) != IOT_OK) {
        LOG_E("encoder", "read fail");   /* read count failed */
        return -1;
    }
    *count = enc->count;
    return IOT_OK;
}

int encoder_get_speed(encoder_t *enc, float dt, float *speed)
{
    if (enc == NULL || speed == NULL || dt <= 0.0f) {
        return -1;
    }
    if (encoder_read(enc, &enc->count) != IOT_OK) {
        return -1;
    }
    enc->speed = (float)(enc->count - enc->last_count) / dt;
    enc->last_count = enc->count;
    *speed = enc->speed;
    return IOT_OK;
}

int encoder_reset(encoder_t *enc)
{
    if (enc == NULL) {
        return -1;
    }
    enc->last_count = enc->count;
    if (enc->ops->reset != NULL) {
        enc->ops->reset(enc->ops->ctx);
    }
    return IOT_OK;
}