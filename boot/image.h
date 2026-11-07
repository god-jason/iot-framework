#ifndef IMAGE_H
#define IMAGE_H

/*
 * 固件镜像头解析与校验。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IMAGE_MAGIC 0x494F5449u   /* "IOTI" */

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
} image_header_t;

typedef enum {
    IMAGE_INVALID,
    IMAGE_VALID,
    IMAGE_PENDING,
    IMAGE_CONFIRMED,
    IMAGE_FAILED
} image_state_t;

int image_parse(const void *data, uint32_t len, image_header_t *header);
int image_verify(const image_header_t *header);
int image_get_state(image_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_H */