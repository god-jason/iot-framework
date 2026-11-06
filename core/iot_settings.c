#include "iot_settings.h"
#include "iot_string.h"
#include "iot_memory.h"

#define IOT_SETTINGS_MAX        32
#define IOT_SETTINGS_BUF_SIZE   512

typedef struct {
    const char *name;
    const void *default_value;
    uint16_t size;
    uint16_t offset;
    bool used;
} settings_entry_t;

static settings_entry_t s_entries[IOT_SETTINGS_MAX];
static uint8_t s_buf[IOT_SETTINGS_BUF_SIZE];
static uint16_t s_buf_used;

static settings_entry_t *settings_find(const char *name)
{
    int i;
    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < IOT_SETTINGS_MAX; i++) {
        if (s_entries[i].used &&
            iot_string_compare(s_entries[i].name, name) == 0) {
            return &s_entries[i];
        }
    }
    return NULL;
}

int iot_settings_init(void)
{
    int i;
    for (i = 0; i < IOT_SETTINGS_MAX; i++) {
        s_entries[i].used = false;
    }
    s_buf_used = 0;
    return IOT_OK;
}

int iot_settings_register(const iot_settings_item_t *item)
{
    int i;
    settings_entry_t *e;

    if (item == NULL || item->name == NULL ||
        item->default_value == NULL || item->size == 0) {
        return IOT_ERR_PARAM;
    }
    if (s_buf_used + item->size > IOT_SETTINGS_BUF_SIZE) {
        return IOT_ERR_OVERFLOW;
    }
    for (i = 0; i < IOT_SETTINGS_MAX; i++) {
        if (!s_entries[i].used) {
            e = &s_entries[i];
            e->name = item->name;
            e->default_value = item->default_value;
            e->size = item->size;
            e->offset = s_buf_used;
            e->used = true;

            iot_memory_copy(&s_buf[e->offset], item->default_value, item->size);
            s_buf_used = (uint16_t)(s_buf_used + item->size);
            return IOT_OK;
        }
    }
    return IOT_ERR_OVERFLOW;
}

int iot_settings_get(const char *name, void *value, uint16_t size)
{
    settings_entry_t *e = settings_find(name);
    if (e == NULL) {
        return IOT_ERR_NOT_FOUND;
    }
    if (value == NULL) {
        return IOT_ERR_PARAM;
    }
    iot_memory_copy(value, &s_buf[e->offset],
                    (size < e->size) ? size : e->size);
    return IOT_OK;
}

int iot_settings_set(const char *name, const void *value, uint16_t size)
{
    settings_entry_t *e = settings_find(name);
    if (e == NULL) {
        return IOT_ERR_NOT_FOUND;
    }
    if (value == NULL) {
        return IOT_ERR_PARAM;
    }
    iot_memory_copy(&s_buf[e->offset], value,
                    (size < e->size) ? size : e->size);
    return IOT_OK;
}

int iot_settings_reset(const char *name)
{
    settings_entry_t *e = settings_find(name);
    if (e == NULL) {
        return IOT_ERR_NOT_FOUND;
    }
    iot_memory_copy(&s_buf[e->offset], e->default_value, e->size);
    return IOT_OK;
}

int iot_settings_reset_all(void)
{
    int i;
    for (i = 0; i < IOT_SETTINGS_MAX; i++) {
        if (s_entries[i].used) {
            iot_memory_copy(&s_buf[s_entries[i].offset],
                            s_entries[i].default_value,
                            s_entries[i].size);
        }
    }
    return IOT_OK;
}