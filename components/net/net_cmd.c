/*
 * net_cmd.c - net 组件的命令注册（ping）
 *
 * 由 net_init() 调用 net_register_commands() 注册到 cmd 全局命令表。
 */

#include "net.h"
#include "ros.h"
#include "common.h"
#include "cmd.h"

typedef struct {
    const char *addr;       /* 已解析的 IP，用于逐包输出 */
} net_ping_ctx_t;

static void net_ping_reply(uint32_t seq, uint32_t rtt, bool ok, void *user)
{
    const net_ping_ctx_t *ctx = (const net_ping_ctx_t *)user;

    if (ok) {
        ros_printf("64 bytes from %s: seq=%u time=%u ms\n",
                   (ctx != NULL) ? ctx->addr : "?", (unsigned)seq,
                   (unsigned)rtt);
    } else {
        ros_printf("request timeout: seq=%u\n", (unsigned)seq);
    }
}

static int net_ping_cmd(const args_parsed_t *parsed, void *user)
{
    const char *host = NULL;
    uint32_t count = 4U;
    uint32_t timeout = 1000U;
    uint32_t interval = 1000U;
    net_ping_ctx_t ctx;
    net_ping_stat_t stat;
    char ip[32];
    int rc;

    (void)user;
    if (!args_get_str(parsed, 0U, &host) || host == NULL) {
        ros_puts("usage: ping <host> [--count|-c n] [--timeout|-t ms] "
                 "[--interval|-i ms]");
        return -1;
    }
    (void)args_get_uint(parsed, 1U, &count);
    (void)args_get_uint(parsed, 2U, &timeout);
    (void)args_get_uint(parsed, 3U, &interval);
    if (count == 0U) {
        count = 4U;
    }

    rc = net_dns_resolve(host, ip, sizeof(ip));
    if (rc != NET_OK) {
        ros_puts("ping: cannot resolve host");
        return -1;
    }
    ros_printf("PING %s (%s)\n", host, ip);

    ctx.addr = ip;
    memset(&stat, 0, sizeof(stat));
    rc = net_ping(ip, count, interval, timeout, net_ping_reply, &ctx, &stat);
    if (rc == NET_ERR_NOT_SUPPORTED) {
        ros_puts("ping: not supported by network backend");
        return -1;
    }
    if (rc != NET_OK) {
        ros_puts("ping: network unavailable");
        return -1;
    }

    ros_printf("--- %s ping statistics ---\n", host);
    ros_printf("%u packets transmitted, %u received, %u%% packet loss\n",
               (unsigned)stat.sent, (unsigned)stat.received,
               (unsigned)((stat.sent != 0U)
                          ? ((stat.sent - stat.received) * 100U / stat.sent)
                          : 0U));
    if (stat.received != 0U) {
        ros_printf("rtt min/avg/max = %u/%u/%u ms\n",
                   (unsigned)stat.min_ms, (unsigned)stat.avg_ms,
                   (unsigned)stat.max_ms);
    }
    return 0;
}

static const args_def_t s_ping_args[] = {
    { "host", 0, ARGS_TYPE_STR, true, true, NULL, "host name or IPv4" },
    { "count", 'c', ARGS_TYPE_UINT, false, false, "4", "echo count" },
    { "timeout", 't', ARGS_TYPE_UINT, false, false, "1000", "timeout ms" },
    { "interval", 'i', ARGS_TYPE_UINT, false, false, "1000", "interval ms" }
};

static const cmd_def_t s_ping_def = {
    "ping", "ICMP echo via net_ping", s_ping_args, 4U
};

void net_register_commands(void)
{
    static bool ready;

    if (!ready) {
        ready = true;
        (void)cmd_register(&s_ping_def, net_ping_cmd, NULL);
    }
}
