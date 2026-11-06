#ifndef IOT_KV_H
#define IOT_KV_H

/*
 * KV 键值存储（默认 RAM 后端，可扩展 Flash/持久化）。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IOT_KV_INT,
    IOT_KV_FLOAT,
    IOT_KV_STRING,
    IOT_KV_BLOB
} iot_kv_type_t;

int iot_kv_init(void);

int iot_kv_set(const char *key, iot_kv_type_t type,
               const void *value, uint32_t len);

int iot_kv_get(const char *key, iot_kv_type_t type,
               void *value, uint32_t size, uint32_t *actual_size);

int iot_kv_delete(const char *key);
int iot_kv_clear(void);
int iot_kv_commit(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_KV_H */