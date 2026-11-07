#include "storage_kv.h"
#include <string.h>

#define STORAGE_KV_MAX      32
#define STORAGE_KV_KEY_MAX  16
#define STORAGE_KV_VAL_MAX  32

typedef struct {
    char key[STORAGE_KV_KEY_MAX];
    storage_kv_type_t type;
    uint8_t value[STORAGE_KV_VAL_MAX];
    uint32_t len;
    bool used;
} storage_kv_entry_t;

static storage_kv_entry_t s_kv[STORAGE_KV_MAX];

static storage_kv_entry_t *storage_kv_find(const char *key)
{
    int i;
    if (key == NULL) {
        return NULL;
    }
    for (i = 0; i < STORAGE_KV_MAX; i++) {
        if (s_kv[i].used && strcmp(s_kv[i].key, key) == 0) {
            return &s_kv[i];
        }
    }
    return NULL;
}

int storage_kv_init(void)
{
    int i;
    for (i = 0; i < STORAGE_KV_MAX; i++) {
        s_kv[i].used = false;
    }
    return IOT_OK;
}

int storage_kv_set(const char *key, storage_kv_type_t type,
               const void *value, uint32_t len)
{
    storage_kv_entry_t *e;
    int i;

    if (key == NULL || value == NULL) {
        return -1;
    }
    if (len > STORAGE_KV_VAL_MAX) {
        return -1;
    }

    e = storage_kv_find(key);
    if (e == NULL) {
        for (i = 0; i < STORAGE_KV_MAX; i++) {
            if (!s_kv[i].used) {
                e = &s_kv[i];
                e->used = true;
                break;
            }
        }
        if (e == NULL) {
            return -1;
        }
    }

    strncpy(e->key, key, STORAGE_KV_KEY_MAX - 1u);
    e->key[STORAGE_KV_KEY_MAX - 1u] = '\0';
    e->type = type;
    memcpy(e->value, value, len);
    e->len = len;
    return IOT_OK;
}

int storage_kv_get(const char *key, storage_kv_type_t type,
               void *value, uint32_t size, uint32_t *actual_size)
{
    storage_kv_entry_t *e = storage_kv_find(key);
    uint32_t n;

    if (e == NULL) {
        return -1;
    }
    if (value == NULL) {
        return -1;
    }
    if (type != e->type) {
        return -1;
    }
    n = (size < e->len) ? size : e->len;
    memcpy(value, e->value, n);
    if (actual_size != NULL) {
        *actual_size = e->len;
    }
    return IOT_OK;
}

int storage_kv_delete(const char *key)
{
    storage_kv_entry_t *e = storage_kv_find(key);
    if (e == NULL) {
        return -1;
    }
    e->used = false;
    return IOT_OK;
}

int storage_kv_clear(void)
{
    int i;
    for (i = 0; i < STORAGE_KV_MAX; i++) {
        s_kv[i].used = false;
    }
    return IOT_OK;
}

int storage_kv_commit(void)
{
    /* RAM 后端无需落盘；预留持久化扩展 */
    return IOT_OK;
}