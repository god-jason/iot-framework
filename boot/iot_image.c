#include "iot_image.h"
#include "iot_memory.h"

static iot_image_state_t s_state = IOT_IMAGE_INVALID;

int iot_image_parse(const void *data, uint32_t len, iot_image_header_t *header)
{
    if (data == NULL || header == NULL || len < sizeof(iot_image_header_t)) {
        return IOT_ERR_PARAM;
    }
    iot_memory_copy(header, data, sizeof(iot_image_header_t));
    return IOT_OK;
}

int iot_image_verify(const iot_image_header_t *header)
{
    if (header == NULL) {
        return IOT_ERR_PARAM;
    }
    s_state = IOT_IMAGE_INVALID;

    if (header->magic != IOT_IMAGE_MAGIC) {
        return IOT_ERR_AUTH;
    }
    if (header->header_size < sizeof(iot_image_header_t)) {
        return IOT_ERR_INVALID;
    }
    if (header->image_size == 0) {
        return IOT_ERR_INVALID;
    }
    s_state = IOT_IMAGE_VALID;
    return IOT_OK;
}

int iot_image_get_state(iot_image_state_t *state)
{
    if (state == NULL) {
        return IOT_ERR_PARAM;
    }
    *state = s_state;
    return IOT_OK;
}