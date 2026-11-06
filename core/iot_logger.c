#include "iot_logger.h"
#include "iot_hex.h"

#include <stdio.h>

#define IOT_LOG_MAX_BACKENDS 4
#define IOT_LOG_BUF_SIZE     256

static iot_log_backend_t s_backends[IOT_LOG_MAX_BACKENDS];
static iot_log_level_t s_level = IOT_LOG_INFO;

int iot_log_init(void)
{
    int i;
    for (i = 0; i < IOT_LOG_MAX_BACKENDS; i++) {
        s_backends[i].output = NULL;
        s_backends[i].tag = NULL;
        s_backends[i].user = NULL;
        s_backends[i].level = IOT_LOG_DEBUG;
    }
    s_level = IOT_LOG_INFO;
    return IOT_OK;
}

int iot_log_register(const iot_log_backend_t *backend)
{
    int i;
    if (backend == NULL || backend->output == NULL) {
        return IOT_ERR_PARAM;
    }
    for (i = 0; i < IOT_LOG_MAX_BACKENDS; i++) {
        if (s_backends[i].output == NULL) {
            s_backends[i] = *backend;
            return IOT_OK;
        }
    }
    return IOT_ERR_OVERFLOW;
}

int iot_log_unregister(iot_log_output_fn output)
{
    int i;
    if (output == NULL) {
        return IOT_ERR_PARAM;
    }
    for (i = 0; i < IOT_LOG_MAX_BACKENDS; i++) {
        if (s_backends[i].output == output) {
            s_backends[i].output = NULL;
            return IOT_OK;
        }
    }
    return IOT_ERR_NOT_FOUND;
}

int iot_log_set_level(iot_log_level_t level)
{
    if (level < IOT_LOG_DEBUG || level > IOT_LOG_FATAL) {
        return IOT_ERR_PARAM;
    }
    s_level = level;
    return IOT_OK;
}

iot_log_level_t iot_log_get_level(void)
{
    return s_level;
}

void iot_log_write(iot_log_level_t level, const char *tag, const char *fmt, ...)
{
    char buf[IOT_LOG_BUF_SIZE];
    va_list ap;
    int i;

    if (level < s_level) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    for (i = 0; i < IOT_LOG_MAX_BACKENDS; i++) {
        if (s_backends[i].output != NULL && level >= s_backends[i].level) {
            s_backends[i].output(level, tag, buf, s_backends[i].user);
        }
    }
}

void iot_log_hexdump(iot_log_level_t level, const char *tag,
                     const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    char line[16 * 3 + 1];
    char msg[128];
    uint32_t i, pos = 0;

    if (level < s_level || data == NULL || len == 0) {
        return;
    }

    for (i = 0; i < len; i++) {
        char hex[3];
        iot_hex_encode(&p[i], 1, hex, sizeof(hex), true);
        pos += (uint32_t)snprintf(line + pos, sizeof(line) - pos, "%s ", hex);
        if ((i + 1) % 16 == 0) {
            snprintf(msg, sizeof(msg), "%04lx: %s",
                     (unsigned long)(i - 15), line);
            iot_log_write(level, tag, "%s", msg);
            pos = 0;
        }
    }
    if (pos != 0) {
        snprintf(msg, sizeof(msg), "%04lx: %s",
                 (unsigned long)((i / 16) * 16), line);
        iot_log_write(level, tag, "%s", msg);
    }
}