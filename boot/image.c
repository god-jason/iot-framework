#include "image.h"
#include <string.h>

static image_state_t s_state = IMAGE_INVALID;

int image_parse(const void *data, uint32_t len, image_header_t *header)
{
    if (data == NULL || header == NULL || len < sizeof(image_header_t)) {
        return -1;
    }
    memcpy(header, data, sizeof(image_header_t));
    return IOT_OK;
}

int image_verify(const image_header_t *header)
{
    if (header == NULL) {
        return -1;
    }
    s_state = IMAGE_INVALID;

    if (header->magic != IMAGE_MAGIC) {
        return -1;
    }
    if (header->header_size < sizeof(image_header_t)) {
        return -1;
    }
    if (header->image_size == 0) {
        return -1;
    }
    s_state = IMAGE_VALID;
    return IOT_OK;
}

int image_get_state(image_state_t *state)
{
    if (state == NULL) {
        return -1;
    }
    *state = s_state;
    return IOT_OK;
}