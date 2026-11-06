#include "iot_kv.h"
#include "iot_string.h"
#include "iot_memory.h"

#define IOT_KV_MAX      32
#define IOT_KV_KEY_MAX  16
#define IOT_KV_VAL_MAX  32

typedef struct {
    char key[IOT_KV_KEY_MAX];
    iot_kv_type_t type;
    uint8_t value[IOT_KV_VAL_MAX];
    uint32_t len;
    bool used;
} kv_entry_t;

static kv_entry_t s_kv[IOT_KV_MAX];

static kv_entry_t *kv_find(const char *key)
{
    int i;
    if (key == NULL) {
        return NULL;
    }
    for (i = 0; i < IOT_KV_MAX; i++) {
        if (s_kv[i].used && iot_string_compare(s_kv[i].key, key) == 0) {
            return &s_kv[i];
        }
    }
    return NULL;
}

int iot_kv_init(void)
{
    int i;
    for (i = 0; i < IOT_KV_MAX; i++) {
        s_kv[i].used = false;
    }
    return IOT_OK;
}

int iot_kv_set(const char *key, iot_kv_type_t type,
               const void *value, uint32_t len)
{
    kv_entry_t *e;
    int i;

    if (key == NULL || value == NULL) {
        return IOT_ERR_PARAM;
    }
    if (len > IOT_KV_VAL_MAX) {
        return IOT_ERR_OVERFLOW;
    }

    e = kv_find(key);
    if (e == NULL) {
        for (i = 0; i < IOT_KV_MAX; i++) {
            if (!s_kv[i].used) {
                e = &s_kv[i];
                e->used = true;
                break;
            }
        }
        if (e == NULL) {
            return IOT_ERR_OVERFLOW;
        }
    }

    iot_string_ncopy(e->key, key, IOT_KV_KEY_MAX);
    e->type = type;
    iot_memory_copy(e->value, value, len);
    e->len = len;
    return IOT_OK;
}

int iot_kv_get(const char *key, iot_kv_type_t type,
               void *value, uint32_t size, uint32_t *actual_size)
{
    kv_entry_t *e = kv_find(key);
    uint32_t n;

    if (e == NULL) {
        return IOT_ERR_NOT_FOUND;
    }
    if (value == NULL) {
        return IOT_ERR_PARAM;
    }
    if (type != e->type) {
        return IOT_ERR_INVALID;
    }
    n = (size < e->len) ? size : e->len;
    iot_memory_copy(value, e->value, n);
    if (actual_size != NULL) {
        *actual_size = e->len;
    }
    return IOT_OK;
}

int iot_kv_delete(const char *key)
{
    kv_entry_t *e = kv_find(key);
    if (e == NULL) {
        return IOT_ERR_NOT_FOUND;
    }
    e->used = false;
    return IOT_OK;
}

int iot_kv_clear(void)
{
    int i;
    for (i = 0; i < IOT_KV_MAX; i++) {
        s_kv[i].used = false;
    }
    return IOT_OK;
}

int iot_kv_commit(void)
{
    /* RAM 后端无需落盘；预留持久化扩展 */
    return IOT_OK;
}