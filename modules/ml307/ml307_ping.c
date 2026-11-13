/*
 * ml307_ping.c - ML307 的 ping 实现（AT+MPING），作为 components/net 后端
 *
 * 命令：AT+MPING="<host>",<count>,<size>,<timeout_s>
 * 结果经 URC 上报，常见格式：
 *   +MPING: "<host>",<seq>,<size>,<rtt_ms>
 * 失败 / 超时行可能包含 TIMEOUT / FAIL / UNREACHABLE 等文本。
 *
 * 本文件仅在 ML307 目标参与编译。
 */

#include "net.h"
#include "ml307.h"
#include "at.h"
#include "at_urc.h"
#include "hal_def.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ML307_PING_URC          "+MPING:"
#define ML307_PING_DEFAULT_MS   1000U
#define ML307_PING_SIZE         64U

typedef struct {
    net_ping_cb cb;
    void *user;
    net_ping_stat_t *stat;
    uint32_t count;
    uint32_t done;
    uint32_t received;
    uint32_t min_ms;
    uint32_t max_ms;
    uint32_t sum_ms;
} ml307_ping_ctx_t;

static ml307_ping_ctx_t s_ping;
static bool s_ping_urc;

/* 大小写不敏感的子串查找 */
static bool ml307_contains_ci(const char *s, const char *needle)
{
    size_t n = strlen(needle);
    size_t i;

    if (s == NULL || n == 0U) {
        return false;
    }
    for (; *s != '\0'; s++) {
        for (i = 0U; i < n; i++) {
            char a = s[i];
            char b = needle[i];

            if (a >= 'A' && a <= 'Z') {
                a = (char)(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = (char)(b - 'A' + 'a');
            }
            if (a != b) {
                break;
            }
        }
        if (i == n) {
            return true;
        }
    }
    return false;
}

static void ml307_ping_on_urc(const char *line, uint32_t len, void *user)
{
    char buf[160];
    char *p;
    char *end;
    long nums[4];
    int found = 0;
    long seq;
    long rtt = -1;
    bool failed;

    (void)user;
    if (line == NULL || len == 0U || s_ping.done >= s_ping.count) {
        return;
    }
    if (len >= sizeof(buf)) {
        len = (uint32_t)sizeof(buf) - 1U;
    }
    memcpy(buf, line, len);
    buf[len] = '\0';
    {
        char *cr = strchr(buf, '\r');

        if (cr != NULL) {
            *cr = '\0';
        }
    }

    failed = ml307_contains_ci(buf, "TIMEOUT") ||
             ml307_contains_ci(buf, "FAIL") ||
             ml307_contains_ci(buf, "UNREACH") ||
             ml307_contains_ci(buf, "ERROR");

    p = strchr(buf, ':');
    if (p == NULL) {
        return;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '"') {
        char *q = strchr(p + 1, '"');

        if (q == NULL) {
            return;
        }
        p = q + 1;
    }

    /* 逐个取出数字字段：序号, 长度, RTT */
    while (*p != '\0' && found < 4) {
        while (*p == ',' || *p == ' ' || *p == '\t') {
            p++;
        }
        if (*p < '0' || *p > '9') {
            break;
        }
        nums[found] = strtol(p, &end, 10);
        if (end == p) {
            break;
        }
        found++;
        p = end;
    }
    if (found == 0) {
        return;
    }

    seq = nums[0];
    if (found >= 2) {
        rtt = nums[found - 1];
    }
    if (seq <= 0 || (uint32_t)seq > s_ping.count) {
        seq = (long)s_ping.done + 1L;
    }
    if (failed || rtt < 0) {
        rtt = -1;
    }

    s_ping.done++;
    if (rtt >= 0) {
        uint32_t v = (uint32_t)rtt;

        s_ping.received++;
        s_ping.sum_ms += v;
        if (s_ping.received == 1U || v < s_ping.min_ms) {
            s_ping.min_ms = v;
        }
        if (v > s_ping.max_ms) {
            s_ping.max_ms = v;
        }
    }
    if (s_ping.cb != NULL) {
        s_ping.cb((uint32_t)seq, (rtt >= 0) ? (uint32_t)rtt : 0U,
                  rtt >= 0, s_ping.user);
    }
}

int net_ping(const char *host, uint32_t count, uint32_t interval_ms,
             uint32_t timeout_ms, net_ping_cb cb, void *user,
             net_ping_stat_t *stat)
{
    char cmd[192];
    uint32_t wait_ms;
    uint32_t start;

    if (host == NULL || host[0] == '\0' || count == 0U ||
        strlen(host) >= 96U || strchr(host, '"') != NULL ||
        strchr(host, '\r') != NULL || strchr(host, '\n') != NULL) {
        return NET_ERR_PARAM;
    }
    if (interval_ms == 0U) {
        interval_ms = ML307_PING_DEFAULT_MS;
    }
    if (timeout_ms == 0U) {
        timeout_ms = ML307_PING_DEFAULT_MS;
    }

    if (!s_ping_urc) {
        if (at_urc_register(ML307_PING_URC, ml307_ping_on_urc, NULL) != 0) {
            return NET_ERR_NOMEM;
        }
        s_ping_urc = true;
    }

    memset(&s_ping, 0, sizeof(s_ping));
    s_ping.cb = cb;
    s_ping.user = user;
    s_ping.stat = stat;
    s_ping.count = count;

    snprintf(cmd, sizeof(cmd), "AT+MPING=\"%s\",%u,%u,%u", host,
             (unsigned)count, (unsigned)ML307_PING_SIZE,
             (unsigned)((timeout_ms + 999U) / 1000U));
    if (at_exec(cmd, 3000U) != AT_RESULT_OK) {
        return NET_ERR_NOT_SUPPORTED;   /* 模组固件不支持 MPING */
    }

    /* 等 URC 上报完：最长 count 个间隔 + 一个超时 + 余量 */
    wait_ms = count * interval_ms + timeout_ms + 2000U;
    start = hal_tick_get();
    while (s_ping.done < count &&
           (uint32_t)(hal_tick_get() - start) < wait_ms) {
        (void)at_process();
        hal_delay_ms(10U);
    }

    /* 没有上报的请求按超时处理 */
    while (s_ping.done < count) {
        uint32_t seq = s_ping.done + 1U;

        s_ping.done++;
        if (cb != NULL) {
            cb(seq, 0U, false, user);
        }
    }

    if (stat != NULL) {
        stat->sent = count;
        stat->received = s_ping.received;
        stat->min_ms = s_ping.min_ms;
        stat->max_ms = s_ping.max_ms;
        stat->avg_ms = (s_ping.received != 0U)
                       ? (s_ping.sum_ms / s_ping.received) : 0U;
    }
    return NET_OK;
}
