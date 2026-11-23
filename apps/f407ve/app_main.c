/*
 * app_main.c - STM32F407VET6 开发板 ML307(4G) 功能演示
 *
 * USART2(PA2/PA3) 连接 ML307R 模组。app_main 由板级 main 创建为独立任务，
 * 这里只负责注册命令；真正的测试在 roshell 会话任务里执行（栈更大）：
 *
 * 注意：F407 的 USART2 走 Cube HAL 轮询收字节（at 引擎已适配），
 *       PA2/PA3 与 PWM 编号 2/3 冲突，接模组时这两路 PWM 不可用。
 *
 *   ml307                一次跑完整套：SIM 信息 -> ping -> http -> tcp -> mqtt
 *   sim                  读取 ICCID / IMEI / IMSI / CSQ / 注册状态 / IP
 *   ping <host>          模组 MPING（由 net_init 注册）
 *   http [url]           HTTP GET，默认 http://www.example.com/
 *   net [host] [port]    TCP 连接/发送/接收，默认 www.baidu.com:80
 *   mqtt [host] [port]   MQTT 连接/订阅/发布/收消息，默认 broker.emqx.io:1883
 */

#include "board.h"
#include "ros.h"
#include "roshell.h"
#include "app.h"
#include "device.h"
#include "fan.h"
#include "stepper.h"
#include "at.h"
#include "ml307.h"
#include "net.h"
#include "http.h"
#include "mqtt.h"
#include "common.h"
#include "fs.h"
#include "w25qxx.h"

#include "stm32_cube.h"

#include <string.h>

#define ML307_RX_BUF_SIZE       64U     /* 单次从 USART2 泵取的字节数 */
#define ML307_LINE_BUF_SIZE     224U

#define DEMO_MQTT_HOST          "broker.emqx.io"
#define DEMO_MQTT_PORT          1883U
#define DEMO_MQTT_TOPIC         "robotos/f407ve/status"
#define DEMO_MQTT_CLIENT_ID     "robotos-f407ve"

#define DEMO_HTTP_URL           "http://www.example.com/"
#define DEMO_TCP_HOST           "www.baidu.com"
#define DEMO_TCP_PORT           80U
#define DEMO_PING_HOST          "www.baidu.com"

static bool s_at_ready;
static uint8_t s_rx_buf[ML307_RX_BUF_SIZE];
static char s_line_buf[ML307_LINE_BUF_SIZE];
static mqtt_client_t s_mqtt;

/* ------------------------------------------------------------------ 初始化 */

/* 首次使用时初始化 AT 引擎、模组握手与 net 后端（ping 命令也随之注册） */
static int demo_ensure(void)
{
    at_config_t cfg;

    if (s_at_ready) {
        return 0;
    }
    ros_puts("ml307: init uart2");
    memset(&cfg, 0, sizeof(cfg));
    cfg.uart = board_uart2();
    cfg.rx_buffer = s_rx_buf;
    cfg.rx_buffer_size = (uint32_t)sizeof(s_rx_buf);
    cfg.line_buffer = s_line_buf;
    cfg.line_buffer_size = (uint32_t)sizeof(s_line_buf);
    cfg.default_timeout = 500U;
    cfg.default_retry = 0U;
    cfg.echo_enabled = 0U;
    if (ml307_init(&cfg) != 0) {
        ros_puts("ml307: init failed");
        return -1;
    }
    if (ml307_start() != 0) {
        ros_puts("ml307: no AT response");
        return -1;
    }
    ros_puts("ml307: ready");
    /* 激活 PDP；模组可能已激活而返回错误，忽略继续 */
    (void)ml307_network_connect(NULL);
    if (net_init() != NET_OK) {
        ros_puts("net: init failed");
        return -1;
    }
    s_at_ready = true;
    return 0;
}

/* ------------------------------------------------------------- SIM 与信号 */

static int run_sim(void)
{
    char buf[64];
    int16_t csq = 0;
    int16_t ber = 0;

    ros_puts("--- SIM / module info ---");
    if (ml307_get_iccid(buf, sizeof(buf)) == 0) {
        ros_printf("iccid : %s\n", buf);
    } else {
        ros_puts("iccid : n/a");
    }
    if (ml307_get_imei(buf, sizeof(buf)) == 0) {
        ros_printf("imei  : %s\n", buf);
    } else {
        ros_puts("imei  : n/a");
    }
    if (ml307_get_imsi(buf, sizeof(buf)) == 0) {
        ros_printf("imsi  : %s\n", buf);
    } else {
        ros_puts("imsi  : n/a");
    }
    if (ml307_get_csq(&csq, &ber) == 0) {
        ros_printf("csq   : %d, ber=%d\n", (int)csq, (int)ber);
    } else {
        ros_puts("csq   : n/a");
    }
    ros_printf("reg   : %s\n", ml307_network_is_registered() ? "registered"
                                                             : "not registered");
    if (ml307_get_ip(buf, sizeof(buf)) == 0) {
        ros_printf("ip    : %s\n", buf);
    } else {
        ros_puts("ip    : n/a");
    }
    return 0;
}

/* ------------------------------------------------------------------- ping */

static void demo_ping_reply(uint32_t seq, uint32_t rtt_ms, bool ok, void *user)
{
    (void)user;
    if (ok) {
        ros_printf("ping: seq=%u time=%u ms\n", (unsigned)seq, (unsigned)rtt_ms);
    } else {
        ros_printf("ping: seq=%u timeout\n", (unsigned)seq);
    }
}

static int run_ping(const char *host, uint32_t count)
{
    net_ping_stat_t stat;
    int rc;

    ros_printf("--- PING %s ---\n", host);
    memset(&stat, 0, sizeof(stat));
    /* 模组 MPING 直接接受域名，内部自行解析，无需先做 DNS */
    rc = net_ping(host, count, 1000U, 1000U, demo_ping_reply, NULL, &stat);
    if (rc != NET_OK) {
        ros_printf("ping: failed (%d)\n", rc);
        return -1;
    }
    ros_printf("ping: sent=%u recv=%u min/avg/max=%u/%u/%u ms\n",
               (unsigned)stat.sent, (unsigned)stat.received,
               (unsigned)stat.min_ms, (unsigned)stat.avg_ms,
               (unsigned)stat.max_ms);
    return (stat.received != 0U) ? 0 : -1;
}

/* ------------------------------------------------------------------- http */

/* 响应体回调：只打印前 160 字节，够验证链路与状态码 */
static int http_show(const uint8_t *data, uint32_t len, void *user)
{
    uint32_t *left = (uint32_t *)user;
    uint32_t n = len;

    if (n > *left) {
        n = *left;
    }
    if (n > 0U) {
        ros_printf("%.*s", (int)n, (const char *)data);
    }
    *left -= n;
    return (*left == 0U) ? 1 : 0;    /* 非 0：中止接收 */
}

static int run_http(const char *url)
{
    http_response_t rsp;
    uint32_t left = 160U;
    int rc;

    ros_printf("--- HTTP GET %s ---\n", url);
    memset(&rsp, 0, sizeof(rsp));
    rc = http_get(url, NULL, 0U, &rsp, http_show, &left, 30000U);
    if (rc != NET_OK) {
        ros_printf("http: failed (%d)\n", rc);
        return -1;
    }
    ros_printf("\nhttp: status=%d len=%u type=%s\n", rsp.status,
               (unsigned)rsp.content_length,
               (rsp.content_type[0] != '\0') ? rsp.content_type : "-");
    return (rsp.status == 200) ? 0 : -1;
}

/* -------------------------------------------------------------- TCP (net) */

static int run_net(const char *host, uint16_t port)
{
    net_sock_t sock;
    uint8_t rx[96];
    char req[96];
    int n;
    uint32_t i;

    ros_printf("--- TCP %s:%u ---\n", host, (unsigned)port);
    if (net_open(&sock, NET_TCP, host, port, 15000U) != NET_OK) {
        ros_puts("net: open failed");
        return -1;
    }
    (void)str_printf(req, sizeof(req), "HEAD / HTTP/1.0\r\nHost: %s\r\n\r\n", host);
    if (net_send(&sock, req, (uint32_t)strlen(req)) <= 0) {
        ros_puts("net: send failed");
        (void)net_close(&sock);
        return -1;
    }
    n = net_recv(&sock, rx, sizeof(rx), 8000U);
    if (n <= 0) {
        ros_printf("net: recv %s (%d)\n", (n == 0) ? "timeout" : "error", n);
        (void)net_close(&sock);
        return -1;
    }
    for (i = 0U; i < (uint32_t)n; i++) {
        if (rx[i] == '\r' || rx[i] == '\n') {
            break;
        }
    }
    ros_printf("net: rx %d bytes, status=[%.*s]\n", n, (int)i, (const char *)rx);
    (void)net_close(&sock);
    return 0;
}

/* ------------------------------------------------------------------- mqtt */

static void mqtt_on_message(const char *topic, const uint8_t *payload,
                            uint32_t len, void *user)
{
    (void)user;
    ros_printf("mqtt: %s = %.*s\n", topic, (int)len, (const char *)payload);
}

static int run_mqtt(const char *host, uint16_t port)
{
    mqtt_options_t opt;
    int i;
    int rc;

    ros_printf("--- MQTT %s:%u ---\n", host, (unsigned)port);
    memset(&opt, 0, sizeof(opt));
    opt.client_id = DEMO_MQTT_CLIENT_ID;
    opt.keepalive_s = 60U;
    opt.clean_session = true;

    (void)mqtt_client_init(&s_mqtt, mqtt_on_message, NULL);
    ros_puts("mqtt: connecting...");
    rc = mqtt_connect(&s_mqtt, host, port, &opt, 20000U);
    if (rc != NET_OK) {
        ros_printf("mqtt: connect failed (%d)\n", rc);
        return -1;
    }
    ros_puts("mqtt: connected");
    if (mqtt_subscribe(&s_mqtt, DEMO_MQTT_TOPIC, 0U) == NET_OK) {
        ros_puts("mqtt: subscribe ok");
    } else {
        ros_puts("mqtt: subscribe failed");
    }
    if (mqtt_publish(&s_mqtt, DEMO_MQTT_TOPIC, "hello", 5U, 0U, false) == NET_OK) {
        ros_puts("mqtt: publish ok");
    } else {
        ros_puts("mqtt: publish failed");
    }
    for (i = 0; i < 50; i++) {
        (void)mqtt_poll(&s_mqtt, 100U);
    }
    (void)mqtt_disconnect(&s_mqtt);
    ros_puts("mqtt: done");
    return 0;
}

/* ------------------------------------------------------------------ 命令 */

static int cmd_all(const rs_cmd_ctx_t *ctx)
{
    (void)ctx;
    if (demo_ensure() != 0) {
        return -1;
    }
    (void)run_sim();
    (void)run_ping(DEMO_PING_HOST, 3U);
    (void)run_http(DEMO_HTTP_URL);
    (void)run_net(DEMO_TCP_HOST, (uint16_t)DEMO_TCP_PORT);
    (void)run_mqtt(DEMO_MQTT_HOST, (uint16_t)DEMO_MQTT_PORT);
    ros_puts("ml307: all tests done");
    return 0;
}

static int cmd_sim(const rs_cmd_ctx_t *ctx)
{
    (void)ctx;
    if (demo_ensure() != 0) {
        return -1;
    }
    return run_sim();
}

static int cmd_http(const rs_cmd_ctx_t *ctx)
{
    const char *url = NULL;

    if (demo_ensure() != 0) {
        return -1;
    }
    (void)rs_args_get_str(ctx->parsed, 0U, &url);
    return run_http((url != NULL && url[0] != '\0') ? url : DEMO_HTTP_URL);
}

static int cmd_net(const rs_cmd_ctx_t *ctx)
{
    const char *host = NULL;
    uint32_t port = 0U;

    if (demo_ensure() != 0) {
        return -1;
    }
    (void)rs_args_get_str(ctx->parsed, 0U, &host);
    (void)rs_args_get_uint(ctx->parsed, 1U, &port);
    if (host == NULL || host[0] == '\0') {
        host = DEMO_TCP_HOST;
    }
    if (port == 0U) {
        port = DEMO_TCP_PORT;
    }
    return run_net(host, (uint16_t)port);
}

static int cmd_mqtt(const rs_cmd_ctx_t *ctx)
{
    const char *host = NULL;
    uint32_t port = 0U;

    if (demo_ensure() != 0) {
        return -1;
    }
    (void)rs_args_get_str(ctx->parsed, 0U, &host);
    (void)rs_args_get_uint(ctx->parsed, 1U, &port);
    if (host == NULL || host[0] == '\0') {
        host = DEMO_MQTT_HOST;
    }
    if (port == 0U) {
        port = DEMO_MQTT_PORT;
    }
    return run_mqtt(host, (uint16_t)port);
}

static const rs_args_t s_http_args[] = {
    { "url", 0, RS_ARGS_TYPE_STR, true, false, DEMO_HTTP_URL, "http url" }
};

static const rs_args_t s_host_port_args[] = {
    { "host", 0, RS_ARGS_TYPE_STR, true, false, DEMO_MQTT_HOST, "host" },
    { "port", 0, RS_ARGS_TYPE_UINT, true, false, "1883", "port" }
};

static const rs_args_t s_net_args[] = {
    { "host", 0, RS_ARGS_TYPE_STR, true, false, DEMO_TCP_HOST, "host" },
    { "port", 0, RS_ARGS_TYPE_UINT, true, false, "80", "port" }
};

static const rs_cmd_t s_all_def = {
    "ml307", "run all ML307 tests (sim/ping/http/tcp/mqtt)", NULL, NULL,
    NULL, 0U, cmd_all, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_sim_def = {
    "sim", "show ICCID/IMEI/IMSI/CSQ/reg/ip", NULL, NULL,
    NULL, 0U, cmd_sim, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_http_def = {
    "http", "HTTP GET test [url]", NULL, NULL,
    s_http_args, 1U, cmd_http, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_net_def = {
    "net", "TCP connect/send/recv test [host] [port]", NULL, NULL,
    s_net_args, 2U, cmd_net, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_mqtt_def = {
    "mqtt", "MQTT connect/sub/pub test [host] [port]", NULL, NULL,
    s_host_port_args, 2U, cmd_mqtt, NULL, NULL, NULL, NULL
};

/* ==========================================================================
 * 板级设备注册（PWM / 风扇 / 步进）与心跳任务
 * ========================================================================== */

/* instance 为定时器编号（0=TIM1，1=TIM2，3=TIM4，11=TIM12） */
typedef struct {
    hal_pwm_t pwm;
    uint8_t   instance;
    uint8_t   channel;
    uint32_t  frequency;
} app_pwm_def_t;

static app_pwm_def_t s_pwms[] = {
    { .instance = 1U,  .channel = 1U, .frequency = 1000U },
    { .instance = 1U,  .channel = 2U, .frequency = 1000U },
    { .instance = 1U,  .channel = 3U, .frequency = 1000U },
    { .instance = 1U,  .channel = 4U, .frequency = 1000U },
    { .instance = 3U,  .channel = 1U, .frequency = 2000U },
    { .instance = 3U,  .channel = 2U, .frequency = 2000U },
    { .instance = 3U,  .channel = 3U, .frequency = 2000U },
    { .instance = 3U,  .channel = 4U, .frequency = 2000U },
    { .instance = 2U,  .channel = 3U, .frequency = 3000U },
    { .instance = 2U,  .channel = 4U, .frequency = 3000U },
    { .instance = 11U, .channel = 1U, .frequency = 4000U },
    { .instance = 11U, .channel = 2U, .frequency = 4000U },
};

static void pwm_gpio_init(GPIO_TypeDef *port, uint16_t pins, uint32_t af)
{
    GPIO_InitTypeDef gpio;

    memset(&gpio, 0, sizeof(gpio));
    gpio.Pin = pins;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = af;
    HAL_GPIO_Init(port, &gpio);
}

/* Cube PWM 初始化钩子：按定时器使能时钟并配置默认 PWM 引脚复用 */
void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2) {
        __HAL_RCC_TIM2_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        pwm_gpio_init(GPIOA, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3,
                      GPIO_AF1_TIM2);
    } else if (htim->Instance == TIM3) {
        __HAL_RCC_TIM3_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        pwm_gpio_init(GPIOB, GPIO_PIN_0 | GPIO_PIN_1, GPIO_AF2_TIM3);
    } else if (htim->Instance == TIM4) {
        __HAL_RCC_TIM4_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        pwm_gpio_init(GPIOB, GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9,
                      GPIO_AF2_TIM4);
    } else if (htim->Instance == TIM12) {
        __HAL_RCC_TIM12_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        pwm_gpio_init(GPIOB, GPIO_PIN_14 | GPIO_PIN_15, GPIO_AF9_TIM12);
    }
}

/* ==========================================================================
 * littlefs：SPI1(PB3/PB4/PB5, AF5) + PB2 片选 + W25Qxx
 * 注：PA6/PA7 被板载 LED 占用，SPI1 使用 PB3/PB4/PB5 复用引脚。
 * ========================================================================== */

#define FS_FLASH_SPI_ID        0U               /* SPI1 */
#define FS_FLASH_CS_PIN        HAL_PIN(1, 2)    /* PB2 */
#define FS_FLASH_BLOCK_SIZE    4096U            /* W25Q 扇区大小 */
#define FS_FLASH_READ_SIZE     256U
#define FS_FLASH_PROG_SIZE     256U
#define FS_FLASH_CACHE_SIZE    256U
#define FS_FLASH_LOOKAHEAD     16U
#define FS_FLASH_SPI_HZ        10000000U

static hal_spi_t         s_fs_spi;
static w25qxx_handle_t   s_fs_flash;
static ros_mutex_t       s_fs_lock;
static struct lfs_config s_lfs_cfg;

/* Cube SPI 初始化钩子：SPI1 复用 PB3(SCK)/PB4(MISO)/PB5(MOSI) */
void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
    GPIO_InitTypeDef gpio;

    if (hspi->Instance == SPI1) {
        __HAL_RCC_SPI1_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();

        memset(&gpio, 0, sizeof(gpio));
        gpio.Pin = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5;
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Pull = GPIO_NOPULL;
        gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio.Alternate = GPIO_AF5_SPI1;
        HAL_GPIO_Init(GPIOB, &gpio);
    }
}

/* littlefs 块设备回调：SPI Flash 为共享总线，统一加锁串行访问 */
static int fs_flash_read(const struct lfs_config *cfg, lfs_block_t block,
                         lfs_off_t off, void *buffer, lfs_size_t size)
{
    uint32_t addr = (uint32_t)block * cfg->block_size + (uint32_t)off;
    int rc;

    (void)ros_mutex_lock(&s_fs_lock, ROS_TIMEOUT_FOREVER);
    rc = w25qxx_read(&s_fs_flash, addr, buffer, (size_t)size);
    (void)ros_mutex_unlock(&s_fs_lock);
    return (rc == HAL_OK) ? 0 : LFS_ERR_IO;
}

static int fs_flash_prog(const struct lfs_config *cfg, lfs_block_t block,
                         lfs_off_t off, const void *buffer, lfs_size_t size)
{
    uint32_t addr = (uint32_t)block * cfg->block_size + (uint32_t)off;
    int rc;

    (void)ros_mutex_lock(&s_fs_lock, ROS_TIMEOUT_FOREVER);
    rc = w25qxx_write(&s_fs_flash, addr, buffer, (size_t)size);
    (void)ros_mutex_unlock(&s_fs_lock);
    return (rc == HAL_OK) ? 0 : LFS_ERR_IO;
}

static int fs_flash_erase(const struct lfs_config *cfg, lfs_block_t block)
{
    int rc;

    (void)ros_mutex_lock(&s_fs_lock, ROS_TIMEOUT_FOREVER);
    rc = w25qxx_erase_sector(&s_fs_flash,
                             (uint32_t)block * cfg->block_size);
    (void)ros_mutex_unlock(&s_fs_lock);
    return (rc == HAL_OK) ? 0 : LFS_ERR_IO;
}

static int fs_flash_sync(const struct lfs_config *cfg)
{
    (void)cfg;
    return 0;                           /* W25Q 写入为同步等待完成 */
}

/* 初始化 SPI Flash 并挂载 littlefs（首次挂载失败时自动格式化） */
static int app_fs_init(void)
{
    hal_spi_config_t spi_cfg;
    fs_config_t fs_cfg;
    int rc;

    (void)ros_mutex_init(&s_fs_lock);

    memset(&spi_cfg, 0, sizeof(spi_cfg));
    spi_cfg.id = FS_FLASH_SPI_ID;
    spi_cfg.mode = HAL_SPI_MODE_MASTER;
    spi_cfg.direction = HAL_SPI_DIRECTION_2LINES;
    spi_cfg.baudrate = FS_FLASH_SPI_HZ;
    spi_cfg.cpol = HAL_SPI_CPOL_LOW;
    spi_cfg.cpha = HAL_SPI_CPHA_1EDGE;
    spi_cfg.bit_order = HAL_SPI_BITORDER_MSB;
    spi_cfg.data_bits = 8U;
    rc = hal_spi_init(&s_fs_spi, &spi_cfg);
    if (rc != HAL_OK) {
        LOG_E("fs", "spi init failed: %d", rc);
        return -1;
    }

    rc = w25qxx_init(&s_fs_flash, &s_fs_spi, FS_FLASH_CS_PIN);
    if (rc != HAL_OK) {
        LOG_E("fs", "spi flash init failed: %d", rc);
        return -1;
    }
    if (s_fs_flash.capacity < FS_FLASH_BLOCK_SIZE) {
        LOG_E("fs", "spi flash capacity invalid");
        return -1;
    }

    memset(&s_lfs_cfg, 0, sizeof(s_lfs_cfg));
    s_lfs_cfg.read = fs_flash_read;
    s_lfs_cfg.prog = fs_flash_prog;
    s_lfs_cfg.erase = fs_flash_erase;
    s_lfs_cfg.sync = fs_flash_sync;
    s_lfs_cfg.read_size = FS_FLASH_READ_SIZE;
    s_lfs_cfg.prog_size = FS_FLASH_PROG_SIZE;
    s_lfs_cfg.block_size = FS_FLASH_BLOCK_SIZE;
    s_lfs_cfg.block_count = s_fs_flash.capacity / FS_FLASH_BLOCK_SIZE;
    s_lfs_cfg.cache_size = FS_FLASH_CACHE_SIZE;
    s_lfs_cfg.lookahead_size = FS_FLASH_LOOKAHEAD;
    s_lfs_cfg.block_cycles = 500;

    memset(&fs_cfg, 0, sizeof(fs_cfg));
    fs_cfg.lfs_cfg = &s_lfs_cfg;
    fs_cfg.auto_format = true;
    rc = fs_init(&fs_cfg);
    if (rc < 0) {
        LOG_E("fs", "littlefs mount failed: %d", rc);
        return -1;
    }

    LOG_I("fs", "littlefs mounted: %u KB, %u blocks",
          (unsigned)(s_fs_flash.capacity / 1024U),
          (unsigned)s_lfs_cfg.block_count);
    return 0;
}

static void app_pwm_register(void)
{
    uint32_t i;

    for (i = 0U; i < sizeof(s_pwms) / sizeof(s_pwms[0]); i++) {
        app_pwm_def_t *def = &s_pwms[i];

        def->pwm.config.channel = def->channel;
        def->pwm.config.id = def->instance;
        def->pwm.config.frequency = def->frequency;
        def->pwm.config.duty = 0U;
        def->pwm.config.polarity = HAL_PWM_POLARITY_HIGH;
        (void)device_pwm_bind(&def->pwm);
    }
}

/* 风扇：复用 PWM 编号 0（TIM2_CH1 / PA0）输出，25kHz */
#define FAN_PWM_SLOT  0U
#define FAN_PWM_FREQ  25000U

static fan_t s_fan;

static int fan_set_duty(void *ctx, uint16_t duty)
{
    hal_pwm_t *pwm = (hal_pwm_t *)ctx;

    if (duty == 0U) {
        if (pwm->base.state == HAL_STATE_RESET) {
            return 0;
        }
        (void)hal_pwm_set_duty(pwm, 0U);
        return (hal_pwm_stop(pwm) == HAL_OK) ? 0 : -1;
    }
    if (pwm->base.state == HAL_STATE_RESET) {
        pwm->config.frequency = FAN_PWM_FREQ;
        pwm->config.duty = 0U;
        if (hal_pwm_init(pwm, &pwm->config) != HAL_OK) {
            return -1;
        }
    }
    if (hal_pwm_set_duty(pwm, (uint32_t)duty * 10U) != HAL_OK) {
        return -1;
    }
    return (hal_pwm_start(pwm) == HAL_OK) ? 0 : -1;
}

static void app_fan_init(void)
{
    static const fan_ops_t ops = { fan_set_duty, &s_pwms[FAN_PWM_SLOT].pwm };

    (void)fan_init(&s_fan, &ops);
}

/* 步进电机：STEP=PB12，DIR=PB13 */
#define STEPPER_STEP_PIN   HAL_PIN(1, 12)   /* PB12: STEP */
#define STEPPER_DIR_PIN    HAL_PIN(1, 13)   /* PB13: DIR */

static stepper_t s_stepper;

static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void dwt_delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000U);

    while ((DWT->CYCCNT - start) < cycles) {
    }
}

static int stepper_set_dir(void *ctx, uint8_t dir)
{
    (void)ctx;
    return (hal_gpio_write(STEPPER_DIR_PIN,
                           (dir != 0U) ? HAL_GPIO_HIGH : HAL_GPIO_LOW) == HAL_OK)
               ? 0 : -1;
}

static void stepper_delay_us(void *ctx, uint32_t us)
{
    (void)ctx;
    dwt_delay_us(us);
}

static int stepper_step(void *ctx)
{
    (void)ctx;
    (void)hal_gpio_write(STEPPER_STEP_PIN, HAL_GPIO_HIGH);
    dwt_delay_us(2U);
    (void)hal_gpio_write(STEPPER_STEP_PIN, HAL_GPIO_LOW);
    return 0;
}

static const stepper_ops_t s_stepper_ops = {
    .set_dir = stepper_set_dir,
    .step = stepper_step,
    .delay_us = stepper_delay_us,
    .ctx = NULL
};

static void app_stepper_init(void)
{
    dwt_init();
    (void)hal_gpio_config_output(STEPPER_STEP_PIN, HAL_GPIO_LOW);
    (void)hal_gpio_config_output(STEPPER_DIR_PIN, HAL_GPIO_LOW);

    if (stepper_init(&s_stepper, &s_stepper_ops) == 0) {
        (void)stepper_set_ramp(&s_stepper, 400U, 100U, 800U, 800U);
    }
}

/* 板载 LED（PA6 / PA7，低电平点亮） */
static void app_led_init(void)
{
    GPIO_InitTypeDef gpio;

    __HAL_RCC_GPIOA_CLK_ENABLE();

    memset(&gpio, 0, sizeof(gpio));
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_SET);
}

static void app_led_toggle(void)
{
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_6 | GPIO_PIN_7);
}

/* 心跳任务：板载 LED 1Hz 闪烁 */
static ros_task_t s_blink_task;
static uint8_t    s_blink_stack[BOARD_BLINK_STACK_SIZE];

static void blink_entry(void *arg)
{
    (void)arg;

    for (;;) {
        app_led_toggle();
        ros_sleep_ms(500U);
    }
}

void app_main(void *arg)
{
    (void)arg;

    app_led_init();

    /* 板级设备：gpio/pwm 命令 + PWM/风扇/步进 */
    if (device_init() == 0) {
        app_pwm_register();
        app_fan_init();
    }
    app_stepper_init();
    (void)app_fs_init();

    /* 心跳：板载 LED 1Hz */
    (void)ros_task_create(&s_blink_task, "blink", 2U, blink_entry, NULL,
                          s_blink_stack, sizeof(s_blink_stack));

    ros_puts("ml307 demo app ready: type 'ml307' to run all tests");

    (void)rs_cmd_register(&s_all_def);
    (void)rs_cmd_register(&s_sim_def);
    (void)rs_cmd_register(&s_http_def);
    (void)rs_cmd_register(&s_net_def);
    (void)rs_cmd_register(&s_mqtt_def);
}
