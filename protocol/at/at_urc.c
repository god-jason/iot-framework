#include "at_urc.h"
#include "iot_string.h"

#define AT_URC_MAX 8

typedef struct {
    const char *prefix;
    at_urc_fn callback;
    void *user;
} at_urc_node_t;

static at_urc_node_t s_nodes[AT_URC_MAX];

int at_urc_register(const char *prefix, at_urc_fn callback, void *user)
{
    int i;
    if (prefix == NULL || callback == NULL) {
        return -1;
    }
    for (i = 0; i < AT_URC_MAX; i++) {
        if (s_nodes[i].callback == NULL) {
            s_nodes[i].prefix = prefix;
            s_nodes[i].callback = callback;
            s_nodes[i].user = user;
            return IOT_OK;
        }
    }
    return -1;
}

int at_urc_unregister(const char *prefix)
{
    int i;
    if (prefix == NULL) {
        return -1;
    }
    for (i = 0; i < AT_URC_MAX; i++) {
        if (s_nodes[i].callback != NULL &&
            iot_string_compare(s_nodes[i].prefix, prefix) == 0) {
            s_nodes[i].prefix = NULL;
            s_nodes[i].callback = NULL;
            s_nodes[i].user = NULL;
            return IOT_OK;
        }
    }
    return -1;
}

int at_urc_dispatch(const char *line, uint32_t len)
{
    int i;
    if (line == NULL) {
        return -1;
    }
    for (i = 0; i < AT_URC_MAX; i++) {
        at_urc_node_t *n = &s_nodes[i];
        uint32_t plen;

        if (n->callback == NULL) {
            continue;
        }
        plen = iot_string_length(n->prefix);
        if (len >= plen && iot_string_ncompare(line, n->prefix, plen) == 0) {
            n->callback(line, len, n->user);
            return IOT_OK;
        }
    }
    return -1;
}