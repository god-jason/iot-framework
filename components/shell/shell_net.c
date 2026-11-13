/*
 * shell_net.c - Shell 网络命令：ping
 *
 * 阻塞执行，输出逐包结果与汇总；可用行首 '-' 放到后台 JOB，例如：
 *   -ping 8.8.8.8 count 10
 */

#include "shell_internal.h"
#include "shell_net.h"
#include "net.h"

#include <stdlib.h>
#include <string.h>

#define PING_USAGE \
    "ping <host> [count <n>] [timeout <ms>] [interval <ms>]"

#define PING_DEFAULT_COUNT     4U
#define PING_DEFAULT_INTERVAL  1000U
#define PING_DEFAULT_TIMEOUT   1000U

typedef struct {
    const char *addr;           /* 解析后的 IP */
} ping_ctx_t;

static void ping_reply(uint32_t seq, uint32_t rtt, bool ok, void *user)
{
    const ping_ctx_t *ctx = (const ping_ctx_t *)user;

    if (ok) {
        ros_printf("64 bytes from %s: seq=%u time=%u ms\n",
                   (ctx != NULL) ? ctx->addr : "?", (unsigned)seq,
                   (unsigned)rtt);
    } else {
        ros_printf("request timeout: seq=%u\n", (unsigned)seq);
    }
}

static int cmd_ping(shell_t *sh, int argc, char **argv)
{
    const char *host;
    uint32_t count = PING_DEFAULT_COUNT;
    uint32_t interval = PING_DEFAULT_INTERVAL;
    uint32_t timeout = PING_DEFAULT_TIMEOUT;
    int i;

    (void)sh;
    if (argc < 2) {
        ros_printf("usage: %s\n", PING_USAGE);
        return -SHELL_ERR_PARAM;
    }
    host = argv[1];

    for (i = 2; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "count") == 0) {
            count = (uint32_t)strtoul(argv[i + 1], NULL, 10);
        } else if (strcmp(argv[i], "timeout") == 0) {
            timeout = (uint32_t)strtoul(argv[i + 1], NULL, 10);
        } else if (strcmp(argv[i], "interval") == 0) {
            interval = (uint32_t)strtoul(argv[i + 1], NULL, 10);
        } else {
            ros_printf("usage: %s\n", PING_USAGE);
            return -SHELL_ERR_PARAM;
        }
    }
    if (count == 0U) {
        count = PING_DEFAULT_COUNT;
    }

    {
        ping_ctx_t ctx;
        net_ping_stat_t stat;
        char ip[32];
        int rc;

        rc = net_dns_resolve(host, ip, sizeof(ip));
        if (rc != NET_OK) {
            ros_puts("ping: cannot resolve host");
            return -SHELL_ERR_NOT_FOUND;
        }
        ros_printf("PING %s (%s)\n", host, ip);

        ctx.addr = ip;
        memset(&stat, 0, sizeof(stat));
        rc = net_ping(ip, count, interval, timeout,
                      ping_reply, &ctx, &stat);
        if (rc == NET_ERR_NOT_SUPPORTED) {
            ros_puts("ping: not supported by network backend");
            return -SHELL_ERR_NOT_FOUND;
        }
        if (rc != NET_OK) {
            ros_puts("ping: network unavailable");
            return -SHELL_ERR_DEVICE;
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
    }
    return SHELL_OK;
}

static const shell_cmd_t s_net_cmds[] = {
    { "ping", cmd_ping, PING_USAGE, "ICMP echo via net_ping", "network",
      CMD_REENTRANT }
};

int shell_net_register(void)
{
    uint32_t i;

    for (i = 0U; i < sizeof(s_net_cmds) / sizeof(s_net_cmds[0]); i++) {
        /* 已存在（重复打开 Shell / 同名命令）时跳过，保证幂等 */
        if (shell_cmd_find(s_net_cmds[i].name) != NULL) {
            continue;
        }
        if (shell_register_cmd(&s_net_cmds[i]) != 0) {
            return -1;
        }
    }
    return 0;
}
