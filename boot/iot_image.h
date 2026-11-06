#ifndef IOT_IMAGE_H
#define IOT_IMAGE_H

/*
 * 固件镜像头解析与校验。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IOT_IMAGE_MAGIC 0x494F5449u   /* "IOTI" */

typedef struct {
    uint32_t magic;
    uint32_t header_size;

    uint32_t product_id;
    uint32_t hardware_id;

    uint32_t version;
    uint32_t image_size;

    uint32_t flags;

    uint8_t sha256[32];
    uint8_t signature[64];
} iot_image_header_t;

typedef enum {
    IOT_IMAGE_INVALID,
    IOT_IMAGE_VALID,
    IOT_IMAGE_PENDING,
    IOT_IMAGE_CONFIRMED,
    IOT_IMAGE_FAILED
} iot_image_state_t;

int iot_image_parse(const void *data, uint32_t len, iot_image_header_t *header);
int iot_image_verify(const iot_image_header_t *header);
int iot_image_get_state(iot_image_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* IOT_IMAGE_H */