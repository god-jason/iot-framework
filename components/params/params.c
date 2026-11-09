/*
 * params.c - 参数表实现
 *
 * 名称采用固定长度拷贝，超长直接拒绝，避免截断后重名。
 * 序列化使用 "%.6g" 以兼顾可读性与精度；反序列化逐行解析。
 */

#include "params.h"

#include <stddef.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 在参数表中查找名称；返回下标，未找到返回 -1 */
static int params_find(const param_store_t *store, const char *name)
{
    uint16_t i;

    if (store == NULL || name == NULL) {
        return -1;
    }
    for (i = 0U; i < PARAM_STORE_MAX; i++) {
        if (store->items[i].used &&
            strcmp(store->items[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

void params_init(param_store_t *store)
{
    if (store == NULL) {
        return;
    }
    memset(store, 0, sizeof(*store));
}

int params_add(param_store_t *store, const char *name,
               float def, float min, float max)
{
    uint16_t i;
    size_t len;

    if (store == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    if (!(min <= def) || !(def <= max)) {
        return -1;
    }
    len = strlen(name);
    if (len >= PARAM_NAME_MAX) {
        return -1;
    }
    if (params_find(store, name) >= 0) {
        return -1;
    }

    for (i = 0U; i < PARAM_STORE_MAX; i++) {
        if (!store->items[i].used) {
            memset(&store->items[i], 0, sizeof(store->items[i]));
            memcpy(store->items[i].name, name, len + 1U);
            store->items[i].value = def;
            store->items[i].def = def;
            store->items[i].min = min;
            store->items[i].max = max;
            store->items[i].used = true;
            store->count++;
            return 0;
        }
    }
    return -1;
}

int params_set(param_store_t *store, const char *name, float value)
{
    int idx;

    idx = params_find(store, name);
    if (idx < 0) {
        return -1;
    }
    if (value < store->items[idx].min || value > store->items[idx].max) {
        return -1;
    }
    store->items[idx].value = value;
    return 0;
}

int params_get(const param_store_t *store, const char *name, float *value)
{
    int idx;

    if (value == NULL) {
        return -1;
    }
    idx = params_find(store, name);
    if (idx < 0) {
        return -1;
    }
    *value = store->items[idx].value;
    return 0;
}

int params_reset(param_store_t *store, const char *name)
{
    int idx;

    idx = params_find(store, name);
    if (idx < 0) {
        return -1;
    }
    store->items[idx].value = store->items[idx].def;
    return 0;
}

void params_reset_all(param_store_t *store)
{
    uint16_t i;

    if (store == NULL) {
        return;
    }
    for (i = 0U; i < PARAM_STORE_MAX; i++) {
        if (store->items[i].used) {
            store->items[i].value = store->items[i].def;
        }
    }
}

int params_serialize(const param_store_t *store, char *buf, uint32_t size)
{
    uint32_t off = 0U;
    uint16_t i;

    if (store == NULL || buf == NULL || size == 0U) {
        return -1;
    }

    for (i = 0U; i < PARAM_STORE_MAX; i++) {
        int n;
        int remain;

        if (!store->items[i].used) {
            continue;
        }
        remain = (int)(size - off);
        if (remain <= 1) {
            return -1;
        }
        n = snprintf(buf + off, (size_t)remain, "%s=%.6g\n",
                     store->items[i].name, (double)store->items[i].value);
        if (n < 0 || n >= remain) {
            return -1;
        }
        off += (uint32_t)n;
    }
    if (off < size) {
        buf[off] = '\0';
    }
    return (int)off;
}

int params_deserialize(param_store_t *store, const char *text)
{
    char line[64];
    uint32_t li = 0U;
    int applied = 0;
    const char *p;

    if (store == NULL || text == NULL || text[0] == '\0') {
        return -1;
    }

    for (p = text; ; p++) {
        char ch = *p;

        if (ch == '\n' || ch == '\0') {
            if (li > 0U) {
                char *eq;
                line[li] = '\0';
                li = 0U;

                if (line[0] != '#') {
                    eq = strchr(line, '=');
                    if (eq != NULL) {
                        char *end = NULL;
                        float v;

                        *eq = '\0';
                        v = strtof(eq + 1, &end);
                        if (end != eq + 1 &&
                            params_set(store, line, v) == 0) {
                            applied++;
                        }
                    }
                }
            }
            if (ch == '\0') {
                break;
            }
            continue;
        }

        if (li < sizeof(line) - 1U) {
            line[li++] = ch;
        }
    }

    return (applied > 0) ? applied : -1;
}
