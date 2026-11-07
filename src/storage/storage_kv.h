#ifndef STORAGE_KV_H
#define STORAGE_KV_H

/*
 * KV 键值存储（默认 RAM 后端，可扩展 Flash/持久化）。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STORAGE_KV_INT,
    STORAGE_KV_FLOAT,
    STORAGE_KV_STRING,
    STORAGE_KV_BLOB
} storage_kv_type_t;

int storage_kv_init(void);

int storage_kv_set(const char *key, storage_kv_type_t type,
               const void *value, uint32_t len);

int storage_kv_get(const char *key, storage_kv_type_t type,
               void *value, uint32_t size, uint32_t *actual_size);

int storage_kv_delete(const char *key);
int storage_kv_clear(void);
int storage_kv_commit(void);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_KV_H */