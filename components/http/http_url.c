/*
 * http_url.c - HTTP URL 解析（通用，所有后端共用）
 */

#include "http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 大小写不敏感比较前 n 个字符 */
static bool http_ci_eq(const char *a, const char *b, uint32_t n)
{
    uint32_t i;

    for (i = 0U; i < n; i++) {
        char ca = a[i];
        char cb = b[i];

        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb || ca == '\0') {
            return false;
        }
    }
    return true;
}

int http_url_parse(const char *url, http_url_t *out)
{
    const char *p;
    const char *host_start;
    const char *host_end;
    size_t host_len;
    size_t path_len;

    if (url == NULL || out == NULL) {
        return NET_ERR_PARAM;
    }
    memset(out, 0, sizeof(*out));
    out->port = 80U;

    if (http_ci_eq(url, "https://", 8U)) {
        p = url + 8;
        out->https = true;
    } else if (http_ci_eq(url, "http://", 7U)) {
        p = url + 7;
    } else {
        return NET_ERR_PARAM;
    }

    host_start = p;
    while (*p != '\0' && *p != ':' && *p != '/') {
        p++;
    }
    host_end = p;
    host_len = (size_t)(host_end - host_start);
    if (host_len == 0U || host_len >= sizeof(out->host)) {
        return NET_ERR_PARAM;
    }
    memcpy(out->host, host_start, host_len);
    out->host[host_len] = '\0';

    if (*p == ':') {
        char *end = NULL;
        unsigned long port;

        p++;
        port = strtoul(p, &end, 10);
        if (end == p || port == 0U || port > 65535U) {
            return NET_ERR_PARAM;
        }
        out->port = (uint16_t)port;
        p = end;
    }

    if (*p == '\0') {
        (void)snprintf(out->path, sizeof(out->path), "/");
    } else if (*p == '/') {
        path_len = strlen(p);
        if (path_len >= sizeof(out->path)) {
            return NET_ERR_PARAM;
        }
        memcpy(out->path, p, path_len + 1U);
    } else {
        return NET_ERR_PARAM;
    }
    return NET_OK;
}
