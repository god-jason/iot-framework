/*
 * alog.c - 异步日志环形缓冲实现
 *
 * 采用 head + count 维护环形区：
 *   最旧字节位置 = (head + size - count) % size。
 * 写入满时前移 head（丢弃最旧），保证新日志优先。
 */

#include "alog.h"

#include <stddef.h>

int alog_init(alog_t *log, char *buf, uint32_t size)
{
    if (log == NULL || buf == NULL || size == 0U) {
        return -1;
    }
    log->buf = buf;
    log->size = size;
    log->head = 0U;
    log->count = 0U;
    log->dropped = 0U;
    return 0;
}

void alog_clear(alog_t *log)
{
    if (log == NULL) {
        return;
    }
    log->head = 0U;
    log->count = 0U;
}

uint32_t alog_write(alog_t *log, const char *data, uint32_t len)
{
    uint32_t i;

    if (log == NULL || data == NULL || len == 0U || log->size == 0U) {
        return 0U;
    }

    for (i = 0U; i < len; i++) {
        if (log->count == log->size) {
            log->head = (log->head + 1U) % log->size;
            log->count--;
            log->dropped++;
        }
        log->buf[(log->head + log->count) % log->size] = data[i];
        log->count++;
    }
    return len;
}

uint32_t alog_read(alog_t *log, char *out, uint32_t max)
{
    uint32_t n;
    uint32_t i;

    if (log == NULL || out == NULL || max == 0U || log->count == 0U) {
        return 0U;
    }

    n = (max < log->count) ? max : log->count;

    /* head 始终指向最旧字节 */
    for (i = 0U; i < n; i++) {
        out[i] = log->buf[(log->head + i) % log->size];
    }

    log->head = (log->head + n) % log->size;
    log->count -= n;
    if (log->count == 0U) {
        log->head = 0U;
    }
    return n;
}

uint32_t alog_count(const alog_t *log)
{
    return (log != NULL) ? log->count : 0U;
}

uint32_t alog_dropped(const alog_t *log)
{
    return (log != NULL) ? log->dropped : 0U;
}
