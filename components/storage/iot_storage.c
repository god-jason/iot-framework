#include "iot_storage.h"
#include "iot_string.h"

#define IOT_STORAGE_MAX 8

typedef struct {
    const char *name;
    const iot_storage_ops_t *ops;
} storage_node_t;

static storage_node_t s_nodes[IOT_STORAGE_MAX];

static const iot_storage_ops_t *storage_find(const char *name)
{
    int i;
    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < IOT_STORAGE_MAX; i++) {
        if (s_nodes[i].ops != NULL &&
            iot_string_compare(s_nodes[i].name, name) == 0) {
            return s_nodes[i].ops;
        }
    }
    return NULL;
}

int iot_storage_register(const char *name, const iot_storage_ops_t *ops)
{
    int i;
    if (name == NULL || ops == NULL) {
        return IOT_ERR_PARAM;
    }
    for (i = 0; i < IOT_STORAGE_MAX; i++) {
        if (s_nodes[i].ops == NULL) {
            s_nodes[i].name = name;
            s_nodes[i].ops = ops;
            if (ops->init != NULL) {
                ops->init(ops->ctx);
            }
            return IOT_OK;
        }
    }
    return IOT_ERR_OVERFLOW;
}

int iot_storage_unregister(const char *name)
{
    int i;
    if (name == NULL) {
        return IOT_ERR_PARAM;
    }
    for (i = 0; i < IOT_STORAGE_MAX; i++) {
        if (s_nodes[i].ops != NULL &&
            iot_string_compare(s_nodes[i].name, name) == 0) {
            s_nodes[i].name = NULL;
            s_nodes[i].ops = NULL;
            return IOT_OK;
        }
    }
    return IOT_ERR_NOT_FOUND;
}

int iot_storage_read(const char *name, uint32_t addr, void *data, uint32_t len)
{
    const iot_storage_ops_t *ops = storage_find(name);
    if (ops == NULL) {
        return IOT_ERR_NOT_FOUND;
    }
    if (ops->read == NULL) {
        return IOT_ERR_NOT_SUPPORTED;
    }
    return ops->read(ops->ctx, addr, data, len);
}

int iot_storage_write(const char *name, uint32_t addr, const void *data, uint32_t len)
{
    const iot_storage_ops_t *ops = storage_find(name);
    if (ops == NULL) {
        return IOT_ERR_NOT_FOUND;
    }
    if (ops->write == NULL) {
        return IOT_ERR_NOT_SUPPORTED;
    }
    return ops->write(ops->ctx, addr, data, len);
}

int iot_storage_erase(const char *name, uint32_t addr, uint32_t len)
{
    const iot_storage_ops_t *ops = storage_find(name);
    if (ops == NULL) {
        return IOT_ERR_NOT_FOUND;
    }
    if (ops->erase == NULL) {
        return IOT_ERR_NOT_SUPPORTED;
    }
    return ops->erase(ops->ctx, addr, len);
}