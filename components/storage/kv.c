#include "kv.h"
#include "iot_string.h"
#include "iot_memory.h"

#define KV_MAX      32
#define KV_KEY_MAX  16
#define KV_VAL_MAX  32

typedef struct {
    char key[KV_KEY_MAX];
    kv_type_t type;
    uint8_t value[KV_VAL_MAX];
    uint32_t len;
    bool used;
} kv_entry_t;

static kv_entry_t s_kv[KV_MAX];

static kv_entry_t *kv_find(const char *key)
{
    int i;
    if (key == NULL) {
        return NULL;
    }
    for (i = 0; i < KV_MAX; i++) {
        if (s_kv[i].used && iot_string_compare(s_kv[i].key, key) == 0) {
            return &s_kv[i];
        }
    }
    return NULL;
}

int kv_init(void)
{
    int i;
    for (i = 0; i < KV_MAX; i++) {
        s_kv[i].used = false;
    }
    return IOT_OK;
}

int kv_set(const char *key, kv_type_t type,
               const void *value, uint32_t len)
{
    kv_entry_t *e;
    int i;

    if (key == NULL || value == NULL) {
        return -1;
    }
    if (len > KV_VAL_MAX) {
        return -1;
    }

    e = kv_find(key);
    if (e == NULL) {
        for (i = 0; i < KV_MAX; i++) {
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

    iot_string_ncopy(e->key, key, KV_KEY_MAX);
    e->type = type;
    iot_memory_copy(e->value, value, len);
    e->len = len;
    return IOT_OK;
}

int kv_get(const char *key, kv_type_t type,
               void *value, uint32_t size, uint32_t *actual_size)
{
    kv_entry_t *e = kv_find(key);
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
    iot_memory_copy(value, e->value, n);
    if (actual_size != NULL) {
        *actual_size = e->len;
    }
    return IOT_OK;
}

int kv_delete(const char *key)
{
    kv_entry_t *e = kv_find(key);
    if (e == NULL) {
        return -1;
    }
    e->used = false;
    return IOT_OK;
}

int kv_clear(void)
{
    int i;
    for (i = 0; i < KV_MAX; i++) {
        s_kv[i].used = false;
    }
    return IOT_OK;
}

int kv_commit(void)
{
    /* RAM 后端无需落盘；预留持久化扩展 */
    return IOT_OK;
}