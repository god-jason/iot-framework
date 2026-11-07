/* iot-os 模拟运行入口：模拟一块硬件，初始化平台与日志控制台，运行功能自检并打印结果。 */
#include "port.h"
#include "log.h"
#include "console.h"
#include "md5.h"
#include "sha256.h"
#include "hex.h"
#include "scheduler.h"
#include "datetime.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %d  %s\n", __LINE__, #cond); g_fail++; } \
} while (0)

static void sim_test_hash(void)
{
    uint8_t d[32];
    char hex[65];

    md5_compute("abc", 3, d);
    hex_encode(d, 16, hex, sizeof(hex), false);
    LOG_I("sim", "md5(abc)=%s", hex);
    CHECK(strcmp(hex, "900150983cd24fb0d6963f7d28e17f72") == 0);

    sha256_compute("abc", 3, d);
    hex_encode(d, 32, hex, sizeof(hex), false);
    LOG_I("sim", "sha256(abc)=%s", hex);
    CHECK(strcmp(hex,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
}

static int cron_hit = 0;
static void cron_cb(void *user) { (void)user; cron_hit++; }

static void sim_test_cron(void)
{
    datetime_t now;
    uint32_t id;

    scheduler_init();
    scheduler_add("0 8 * * *", cron_cb, NULL, &id);

    memset(&now, 0, sizeof(now));
    now.year = 2024; now.month = 1; now.day = 1;
    now.hour = 8; now.minute = 0; now.second = 0;
    scheduler_process(&now);
    CHECK(cron_hit == 1);
    LOG_I("sim", "cron fired");
}

int main(void)
{
    uint8_t rx[64];
    port_uart_handle_t uart;
    port_uart_config_t uart_cfg;
    console_config_t cfg;

    /* 初始化模拟平台 */
    if (port_init() != PORT_OK) {
        printf("port_init failed\n");
        return 1;
    }

    /* 日志宏直接输出到 stdout */
    memset(&uart, 0, sizeof(uart));
    memset(&uart_cfg, 0, sizeof(uart_cfg));
    uart.base.instance = (void *)1;
    uart_cfg.baudrate = 115200;
    uart_cfg.word_length = PORT_UART_WORD_8BIT;
    uart_cfg.parity = PORT_UART_PARITY_NONE;
    uart_cfg.stop_bits = PORT_UART_STOP_1;
    uart_cfg.flow_control = PORT_UART_FLOW_NONE;
    uart_cfg.mode = PORT_UART_MODE_TX_RX;
    uart_cfg.oversampling = false;
    (void)port_uart_init(&uart, &uart_cfg);

    cfg.uart = &uart;
    cfg.rx_buffer = rx;
    cfg.rx_buffer_size = sizeof(rx);
    console_init(&cfg);
    console_start();

    LOG_I("sim", "simulator boot, tick=%u", (unsigned)port_tick_get());

    sim_test_hash();
    sim_test_cron();

    if (g_fail == 0) {
        LOG_I("sim", "ALL SIMULATION TESTS PASSED");
        return 0;
    }
    LOG_E("sim", "%d test(s) failed", g_fail);
    return 1;
}
