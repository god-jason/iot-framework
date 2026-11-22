/*
 * ml307_ping.c - ML307R 的 ping 实现（AT+MPING），作为 components/net 后端
 *
 * 指令（TCP/IP 用户手册 V5.0.0）：
 *   AT+MPING="<host>"[,<timeout>[,<ping_num>[,<packet_len>]]]
 *     <timeout>    单位 s，1~60，默认 10
 *     <ping_num>   次数，1~65535，默认 4
 *     <packet_len> 包长，1~1400，默认 16
 * 结果 URC：
 *   +MPING: <result>,"<ip>",<packet_len>,<time>,<ttl>      单包，result=0 成功
 *   +MPING: "statistics",<sent>,<lost>,<min>,<max>,<avg>   多包统计
 *
 * 本文件仅在 ML307 目标参与编译。
 */

#include "net.h"
#include "ml307.h"
#include "at.h"
#include "at_urc.h"
#include "hal_def.h"
#include "ros.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ML307_PING_URC          "+MPING:"
#define ML307_PING_DEFAULT_MS   1000U
#define ML307_PING_PACKET_LEN   64U
#define ML307_PING_TIMEOUT_MAX  60U

typedef struct {
    net_ping_cb cb;
    void *user;
    uint32_t count;
    uint32_t done;
    uint32_t received;
    uint32_t min_ms;
    uint32_t max_ms;
    uint32_t sum_ms;
    bool have_stat;     /* 收到 "statistics" 汇总行 */
    bool finished;
    uint32_t stat_sent;
    uint32_t stat_lost;
    uint32_t stat_min;
    uint32_t stat_max;
    uint32_t stat_avg;
} ml307_ping_ctx_t;

static ml307_ping_ctx_t s_ping;
static bool s_ping_urc;

/* 跳过分隔符（空格 / 逗号 / 制表符） */
static const char *ml307_ping_skip(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == ',')) {
        p++;
    }
    return p;
}

/* 解析一个十进制字段，p 推进到字段之后 */
static const char *ml307_ping_int(const char *p, const char *end, int *out)
{
    int v = 0;

    p = ml307_ping_skip(p, end);
    if (p >= end || *p < '0' || *p > '9') {
        *out = 0;
        return p;
    }
    while (p < end && *p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
    }
    *out = v;
    return p;
}

static void ml307_ping_statistics(const char *p, const char *end)
{
    int v[5];
    int i;

    for (i = 0; i < 5; i++) {
        p = ml307_ping_int(p, end, &v[i]);
    }
    s_ping.have_stat = true;
    s_ping.stat_sent = (v[0] > 0) ? (uint32_t)v[0] : 0U;
    s_ping.stat_lost = (v[1] > 0) ? (uint32_t)v[1] : 0U;
    s_ping.stat_min = (v[2] > 0) ? (uint32_t)v[2] : 0U;
    s_ping.stat_max = (v[3] > 0) ? (uint32_t)v[3] : 0U;
    s_ping.stat_avg = (v[4] > 0) ? (uint32_t)v[4] : 0U;
    s_ping.finished = true;
}

/*
 * +MPING: <result>,"<ip>",<packet_len>,<time>,<ttl>
 * result=0 成功；1 DNS 失败、2 DNS 超时、3 响应错误、4 超时、5 其他。
 */
static void ml307_ping_on_urc(const char *line, uint32_t len, void *user)
{
    const char *p;
    const char *end = line + len;
    int result;
    int rtt = 0;
    int tmp;
    uint32_t seq;

    (void)user;
    if (line == NULL || len < 8U || !str_starts_with(line, ML307_PING_URC)) {
        return;
    }
    p = ml307_ping_skip(line + 7, end);
    if (p >= end) {
        return;
    }

    if (*p == '"') {
        /* +MPING: "statistics",<sent>,<lost>,<min>,<max>,<avg> */
        if ((size_t)(end - p) >= 12U && strncmp(p + 1, "statistics", 10U) == 0) {
            ml307_ping_statistics(p + 12, end);
        }
        return;
    }
    if (s_ping.finished || s_ping.done >= s_ping.count) {
        return;
    }

    p = ml307_ping_int(p, end, &result);
    p = ml307_ping_skip(p, end);
    if (p < end && *p == '"') {
        /* 跳过 ip 字符串 */
        p++;
        while (p < end && *p != '"') {
            p++;
        }
        if (p < end) {
            p++;
        }
    }
    p = ml307_ping_int(p, end, &tmp);   /* packet_len */
    p = ml307_ping_int(p, end, &rtt);   /* time(ms) */
    (void)ml307_ping_int(p, end, &tmp); /* ttl */

    seq = s_ping.done + 1U;
    s_ping.done++;
    if (result == 0) {
        uint32_t v = (rtt > 0) ? (uint32_t)rtt : 0U;

        s_ping.received++;
        s_ping.sum_ms += v;
        if (s_ping.received == 1U || v < s_ping.min_ms) {
            s_ping.min_ms = v;
        }
        if (v > s_ping.max_ms) {
            s_ping.max_ms = v;
        }
        if (s_ping.cb != NULL) {
            s_ping.cb(seq, v, true, s_ping.user);
        }
    } else if (s_ping.cb != NULL) {
        s_ping.cb(seq, 0U, false, s_ping.user);
    }
}

int net_ping(const char *host, uint32_t count, uint32_t interval_ms,
             uint32_t timeout_ms, net_ping_cb cb, void *user,
             net_ping_stat_t *stat)
{
    char cmd[192];
    uint32_t wait_ms;
    uint32_t start;
    uint32_t timeout_s;

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
    timeout_s = (timeout_ms + 999U) / 1000U;
    if (timeout_s == 0U) {
        timeout_s = 1U;
    }
    if (timeout_s > ML307_PING_TIMEOUT_MAX) {
        timeout_s = ML307_PING_TIMEOUT_MAX;
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
    s_ping.count = count;

    str_printf(cmd, sizeof(cmd), "AT+MPING=\"%s\",%u,%u,%u", host,
             (unsigned)timeout_s, (unsigned)count,
             (unsigned)ML307_PING_PACKET_LEN);
    if (at_exec(cmd, 3000U) != AT_RESULT_OK) {
        return NET_ERR_NOT_SUPPORTED;   /* 模组固件不支持 MPING */
    }

    /* 等 URC 上报完：最长 count 个间隔 + 一个超时 + 余量 */
    wait_ms = count * interval_ms + timeout_ms * 1000U + 3000U;
    start = hal_tick_get();
    while (!s_ping.finished && s_ping.done < count &&
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
        if (s_ping.have_stat) {
            stat->sent = s_ping.stat_sent;
            stat->received = (s_ping.stat_sent > s_ping.stat_lost)
                             ? (s_ping.stat_sent - s_ping.stat_lost) : 0U;
            stat->min_ms = s_ping.stat_min;
            stat->max_ms = s_ping.stat_max;
            stat->avg_ms = s_ping.stat_avg;
        } else {
            stat->sent = count;
            stat->received = s_ping.received;
            stat->min_ms = s_ping.min_ms;
            stat->max_ms = s_ping.max_ms;
            stat->avg_ms = (s_ping.received != 0U)
                           ? (s_ping.sum_ms / s_ping.received) : 0U;
        }
    }
    return NET_OK;
}
