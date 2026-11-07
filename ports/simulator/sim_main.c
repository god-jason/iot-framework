/* iot-os 模拟运行入口：模拟一块硬件，初始化平台与日志控制台，运行功能自检并打印结果。 */
#include "iot_port.h"
#include "iot_port_sim.h"
#include "iot_logger.h"
#include "iot_console.h"
#include "md5.h"
#include "sha256.h"
#include "iot_hex.h"
#include "iot_cron.h"
#include "iot_datetime.h"

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
    iot_hex_encode(d, 16, hex, sizeof(hex), false);
    LOG_I("sim", "md5(abc)=%s", hex);
    CHECK(strcmp(hex, "900150983cd24fb0d6963f7d28e17f72") == 0);

    sha256_compute("abc", 3, d);
    iot_hex_encode(d, 32, hex, sizeof(hex), false);
    LOG_I("sim", "sha256(abc)=%s", hex);
    CHECK(strcmp(hex,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
}

static int cron_hit = 0;
static void cron_cb(void *user) { (void)user; cron_hit++; }

static void sim_test_cron(void)
{
    iot_datetime_t now;
    iot_id_t id;

    iot_cron_init();
    iot_cron_add("08:00:00", cron_cb, NULL, &id);

    memset(&now, 0, sizeof(now));
    now.year = 2024; now.month = 1; now.day = 1;
    now.hour = 8; now.minute = 0; now.second = 0;
    iot_cron_process(&now);
    CHECK(cron_hit == 1);
    LOG_I("sim", "cron fired");
}

int main(void)
{
    uint8_t rx[64];
    iot_console_config_t cfg;

    /* 初始化模拟平台 */
    iot_port_init(iot_sim_port_get());

    /* 日志 -> 控制台 -> 模拟 UART(stdout) */
    iot_log_init();
    cfg.uart = iot_sim_uart_get();
    cfg.rx_buffer = rx;
    cfg.rx_buffer_size = sizeof(rx);
    iot_console_init(&cfg);
    iot_console_start();

    LOG_I("sim", "simulator boot, tick=%u", (unsigned)iot_port_get()->system.tick_get());

    sim_test_hash();
    sim_test_cron();

    if (g_fail == 0) {
        LOG_I("sim", "ALL SIMULATION TESTS PASSED");
        return 0;
    }
    LOG_E("sim", "%d test(s) failed", g_fail);
    return 1;
}