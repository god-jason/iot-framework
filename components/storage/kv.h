#ifndef KV_H
#define KV_H

/*
 * KV 键值存储（默认 RAM 后端，可扩展 Flash/持久化）。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KV_INT,
    KV_FLOAT,
    KV_STRING,
    KV_BLOB
} kv_type_t;

int kv_init(void);

int kv_set(const char *key, kv_type_t type,
               const void *value, uint32_t len);

int kv_get(const char *key, kv_type_t type,
               void *value, uint32_t size, uint32_t *actual_size);

int kv_delete(const char *key);
int kv_clear(void);
int kv_commit(void);

#ifdef __cplusplus
}
#endif

#endif /* KV_H */