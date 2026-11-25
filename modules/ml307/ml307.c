#include "ml307.h"
#include "ros.h"
#include "common.h"
#include "at_urc.h"
#include "hal_def.h"
#include "datetime.h"

#include <stdlib.h>

static ml307_state_t s_state = ML307_STATE_OFF;
static bool s_connected = false;

/* 从响应中提取一段连续数字（最少 min_digits 位） */
static int extract_digits(const char *resp, char *out, uint32_t size, uint32_t min_digits)
{
    const char *p = resp;
    uint32_t i = 0;
    uint32_t run = 0;
    const char *run_start = NULL;

    if (resp == NULL || out == NULL || size == 0) {
        return -1;
    }
    while (*p != '\0') {
        if (*p >= '0' && *p <= '9') {
            if (run == 0) {
                run_start = p;
            }
            run++;
        } else {
            if (run >= min_digits) {
                break;
            }
            run = 0;
            run_start = NULL;
        }
        p++;
    }
    if (run_start == NULL || run < min_digits) {
        return -1;
    }
    for (i = 0; i < run && i < size - 1; i++) {
        out[i] = run_start[i];
    }
    out[i] = '\0';
    return 0;
}

int ml307_init(const at_config_t *config)
{
    if (config == NULL) {
        return -1;
    }
    s_state = ML307_STATE_OFF;
    s_connected = false;
    return at_init(config);
}

int ml307_start(void)
{
    ml307_state_t prev = s_state;
    (void)prev;

    if (at_start() != 0) {
        return -1;
    }
    s_state = ML307_STATE_STARTING;

    /* AT 连通性测试 + 关闭回显 */
    if (at_exec("AT", 1500) != AT_RESULT_OK) {
        s_state = ML307_STATE_ERROR;
        return -1;
    }
    at_exec("ATE0", 2000);

    s_state = ML307_STATE_READY;
    return 0;
}

int ml307_stop(void)
{
    at_stop();
    s_state = ML307_STATE_OFF;
    s_connected = false;
    return 0;
}

int ml307_reset(void)
{
    (void)at_exec("AT+RESET", 3000);
    s_state = ML307_STATE_OFF;
    s_connected = false;
    return 0;
}

int ml307_process(void)
{
    return at_process();
}

ml307_state_t ml307_get_state(void)
{
    return s_state;
}

int ml307_get_imei(char *buffer, uint32_t size)
{
    const char *resp;

    if (at_exec("AT+CGSN=1", 3000) != AT_RESULT_OK) {
        return -1;
    }
    resp = at_get_response();
    return extract_digits(resp, buffer, size, 14);
}

int ml307_get_iccid(char *buffer, uint32_t size)
{
    const char *resp;

    if (at_exec("AT+ICCID", 3000) != AT_RESULT_OK) {
        return -1;
    }
    resp = at_get_response();
    return extract_digits(resp, buffer, size, 10);
}

int ml307_get_imsi(char *buffer, uint32_t size)
{
    const char *resp;

    if (at_exec("AT+CIMI", 3000) != AT_RESULT_OK) {
        return -1;
    }
    resp = at_get_response();
    return extract_digits(resp, buffer, size, 10);
}

/*
 * 解析 "+CSQ: <rssi>[,<ber>]"。
 * 返回解析到的字段数（0 表示无有效数据）；不引入 libc scanf，省 Flash。
 */
static int ml307_parse_csq(const char *resp, int *rssi, int *ber)
{
    const char *p;
    int n;

    if (resp == NULL || rssi == NULL) {
        return 0;
    }
    p = strstr(resp, "+CSQ:");
    if (p == NULL) {
        return 0;
    }
    p += 5;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p < '0' || *p > '9') {
        return 0;
    }
    *rssi = 0;
    while (*p >= '0' && *p <= '9') {
        *rssi = *rssi * 10 + (*p - '0');
        p++;
    }
    n = 1;

    if (ber != NULL) {
        *ber = -1;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == ',') {
            p++;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (*p >= '0' && *p <= '9') {
                *ber = 0;
                while (*p >= '0' && *p <= '9') {
                    *ber = *ber * 10 + (*p - '0');
                    p++;
                }
                n = 2;
            }
        }
    }
    return n;
}

int ml307_get_signal(ml307_info_t *info)
{
    const char *resp;
    int rssi = 0;

    if (info == NULL) {
        return -1;
    }
    if (at_exec("AT+CSQ", 3000) != AT_RESULT_OK) {
        return -1;
    }
    resp = at_get_response();
    if (ml307_parse_csq(resp, &rssi, NULL) >= 1) {
        /* 0..31 映射到 dBm（约 -113 + rssi*2） */
        info->rssi = (int16_t)(-113 + rssi * 2);
        return 0;
    }
    return -1;
}

int ml307_get_csq(int16_t *csq, int16_t *ber)
{
    const char *resp;
    int c = -1;
    int b = -1;

    if (csq == NULL) {
        return -1;
    }
    if (at_exec("AT+CSQ", 3000) != AT_RESULT_OK) {
        return -1;
    }
    resp = at_get_response();
    if (ml307_parse_csq(resp, &c, &b) < 1) {
        return -1;
    }
    if (c < 0 || (c > 31 && c != 99)) {
        return -1;      /* 非法 CSQ（保留 99 表示未知） */
    }
    *csq = (int16_t)c;
    if (ber != NULL) {
        *ber = (int16_t)b;
    }
    return 0;
}

/* 参数是否可安全放入 AT 命令的引号内 */
static bool at_quoted_param_valid(const char *s, size_t max_len)
{
    size_t len;

    if (s == NULL) {
        return true;
    }
    len = strlen(s);
    if (len >= max_len) {
        return false;
    }
    return strchr(s, '"') == NULL && strchr(s, '\\') == NULL &&
           strchr(s, '\r') == NULL && strchr(s, '\n') == NULL;
}

int ml307_set_apn(const char *apn, const char *username, const char *password)
{
    char cmd[192];

    if (apn == NULL || apn[0] == '\0' || !at_quoted_param_valid(apn, 64U) ||
        !at_quoted_param_valid(username, 64U) ||
        !at_quoted_param_valid(password, 64U)) {
        return -1;
    }

    str_printf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn);
    if (at_exec(cmd, 5000) != AT_RESULT_OK) {
        return -1;
    }

    if ((username != NULL && username[0] != '\0') ||
        (password != NULL && password[0] != '\0')) {
        str_printf(cmd, sizeof(cmd), "AT+CGAUTH=1,1,\"%s\",\"%s\"",
                 (username != NULL) ? username : "",
                 (password != NULL) ? password : "");
        if (at_exec(cmd, 5000) != AT_RESULT_OK) {
            return -1;
        }
    }
    return 0;
}

/* ---------------------------- 网络时间（AT+MNTP） ---------------------------- */

#define ML307_NTP_URC           "+MNTP:"
#define ML307_NTP_DEFAULT_SRV   "ntp1.aliyun.com"
#define ML307_NTP_PORT          123U
#define ML307_NTP_TIMEOUT_S     30U
#define ML307_NTP_WAIT_MS       35000U
#define ML307_NTP_TIME_MAX      32U

static bool s_ntp_urc;
static volatile bool s_ntp_done;
static volatile int s_ntp_result;
static char s_ntp_time[ML307_NTP_TIME_MAX];

/* 读取两位十进制数字，失败返回 -1 */
static int ml307_digit2(const char **pp)
{
    const char *p = *pp;

    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') {
        return -1;
    }
    *pp = p + 2;
    return (p[0] - '0') * 10 + (p[1] - '0');
}

/* 期望下一个字符为 c */
static int ml307_expect(const char **pp, char c)
{
    if (**pp != c) {
        return -1;
    }
    (*pp)++;
    return 0;
}

/*
 * 解析模组时间串 "yy/MM/dd,hh:mm:ss±zz"（zz 为时区的 4 倍）。
 * 实测 ML307R 返回的日期时间是 UTC，±zz 表示所在时区（+32 = UTC+8），
 * 因此加上时区偏移得到本地墙钟时间，再写入 RTC。
 */
static int ml307_ntp_to_local(const char *s, uint32_t *time)
{
    const char *p = s;
    datetime_t dt;
    int64_t ts;
    int v;
    int tz;
    int sign = 1;

    memset(&dt, 0, sizeof(dt));

    if ((v = ml307_digit2(&p)) < 0) return -1;
    dt.year = (uint16_t)(2000 + v);
    if (ml307_expect(&p, '/') != 0) return -1;
    if ((v = ml307_digit2(&p)) < 0) return -1;
    dt.month = (uint8_t)v;
    if (ml307_expect(&p, '/') != 0) return -1;
    if ((v = ml307_digit2(&p)) < 0) return -1;
    dt.day = (uint8_t)v;
    if (ml307_expect(&p, ',') != 0) return -1;
    if ((v = ml307_digit2(&p)) < 0) return -1;
    dt.hour = (uint8_t)v;
    if (ml307_expect(&p, ':') != 0) return -1;
    if ((v = ml307_digit2(&p)) < 0) return -1;
    dt.minute = (uint8_t)v;
    if (ml307_expect(&p, ':') != 0) return -1;
    if ((v = ml307_digit2(&p)) < 0) return -1;
    dt.second = (uint8_t)v;

    if (*p == '+') {
        p++;
    } else if (*p == '-') {
        sign = -1;
        p++;
    } else {
        return -1;
    }
    if ((v = ml307_digit2(&p)) < 0) return -1;
    tz = sign * v;                          /* 例如 +32 = UTC+8 */

    if (!datetime_is_valid(&dt) ||
        datetime_to_timestamp(&dt, &ts) != 0) {
        return -1;
    }
    ts += (int64_t)tz * 900;                /* UTC -> 本地时间 */
    if (ts < 0 || ts > (int64_t)0xFFFFFFFF) {
        return -1;
    }
    *time = (uint32_t)ts;
    return 0;
}

/* +MNTP: <result>[,"yy/MM/dd,hh:mm:ss±zz"] */
static void ml307_ntp_urc(const char *line, uint32_t len, void *user)
{
    const char *p;
    const char *end = line + len;
    int result = 0;
    uint32_t n = 0U;

    (void)user;
    if (line == NULL || len < 8U || !str_starts_with(line, ML307_NTP_URC)) {
        return;
    }
    p = line + 7;
    while (p < end && *p >= '0' && *p <= '9') {
        result = result * 10 + (*p - '0');
        p++;
    }
    while (p < end && (*p == ' ' || *p == '\t' || *p == ',')) {
        p++;
    }
    if (p < end && *p == '"') {
        p++;
        while (p < end && *p != '"' && n + 1U < ML307_NTP_TIME_MAX) {
            s_ntp_time[n++] = *p++;
        }
    }
    s_ntp_time[n] = '\0';
    s_ntp_result = result;
    s_ntp_done = true;
}

int ml307_get_ntp_time(const char *server, uint32_t *time)
{
    char cmd[128];
    uint32_t start;
    int n;

    if (time == NULL) {
        return -1;
    }
    if (server == NULL || server[0] == '\0') {
        server = ML307_NTP_DEFAULT_SRV;
    }
    if (!at_quoted_param_valid(server, 96U)) {
        return -1;
    }

    if (!s_ntp_urc) {
        if (at_urc_register(ML307_NTP_URC, ml307_ntp_urc, NULL) != 0) {
            return -1;
        }
        s_ntp_urc = true;
    }

    s_ntp_done = false;
    s_ntp_result = -1;
    s_ntp_time[0] = '\0';

    n = str_printf(cmd, sizeof(cmd), "AT+MNTP=\"%s\",%u,0,%u", server,
                   (unsigned)ML307_NTP_PORT, (unsigned)ML307_NTP_TIMEOUT_S);
    if (n <= 0 || n >= (int)sizeof(cmd)) {
        return -1;
    }
    if (at_exec(cmd, 5000U) != AT_RESULT_OK) {
        return -1;
    }

    start = hal_tick_get();
    while (!s_ntp_done &&
           (uint32_t)(hal_tick_get() - start) < ML307_NTP_WAIT_MS) {
        (void)at_process();
        ros_sleep_ms(10U);
    }
    if (!s_ntp_done || s_ntp_result != 0 || s_ntp_time[0] == '\0') {
        return -1;
    }
    return ml307_ntp_to_local(s_ntp_time, time);
}

/* 读取一个逗号分隔字段，支持可选的成对引号 */
static const char *wifi_next_field(const char *p, char *out, size_t cap)
{
    size_t n = 0U;
    bool quoted = false;

    p = str_skip_ws(p);
    if (*p == '"') {
        quoted = true;
        p++;
    }
    while (*p != '\0') {
        if (quoted) {
            if (*p == '"') {
                p++;
                break;
            }
        } else if (*p == ',') {
            break;
        }
        if (n + 1U < cap) {
            out[n++] = *p;
        }
        p++;
    }
    out[n] = '\0';
    if (*p == ',') {
        p++;
    }
    return p;
}

/* 解析单行 WiFi 扫描结果：+WIFISCAN: "ssid",bssid,rssi,channel */
static int parse_wifi_line(const char *line, ml307_wifi_ap_t *ap)
{
    const char *p;
    char num[8];
    long value;

    p = strstr(line, "+WIFISCAN:");
    if (p == NULL) {
        p = strstr(line, "+WIFI:");
    }
    if (p == NULL) {
        return -1;
    }
    p = strchr(p, ':');
    if (p == NULL) {
        return -1;
    }
    p++;

    p = wifi_next_field(p, ap->ssid, sizeof(ap->ssid));
    if (ap->ssid[0] == '\0') {
        return -1;
    }
    p = wifi_next_field(p, ap->bssid, sizeof(ap->bssid));
    if (ap->bssid[0] == '\0') {
        return -1;
    }
    p = wifi_next_field(p, num, sizeof(num));
    value = strtol(num, NULL, 10);
    if (value < -120 || value > 0) {
        return -1;
    }
    ap->rssi = (int16_t)value;

    p = wifi_next_field(p, num, sizeof(num));
    value = strtol(num, NULL, 10);
    if (value < 0 || value > 255) {
        return -1;
    }
    ap->channel = (uint8_t)value;
    return 0;
}

int ml307_get_wifi_list(ml307_wifi_ap_t *aps, uint8_t max_count, uint8_t *count)
{
    const char *resp;
    const char *p;
    char line[192];
    uint8_t n = 0U;

    if (count == NULL || (aps == NULL && max_count > 0U)) {
        return -1;
    }
    *count = 0U;
    if (max_count == 0U) {
        return 0;
    }
    if (at_exec("AT+WIFISCAN", 15000) != AT_RESULT_OK) {
        return -1;
    }

    resp = at_get_response();
    if (resp == NULL) {
        return 0;
    }

    p = resp;
    while (*p != '\0' && n < max_count) {
        const char *eol = strchr(p, '\n');
        size_t len = (eol != NULL) ? (size_t)(eol - p) : strlen(p);
        char *cr;

        if (len >= sizeof(line)) {
            len = sizeof(line) - 1U;
        }
        memcpy(line, p, len);
        line[len] = '\0';
        cr = strchr(line, '\r');
        if (cr != NULL) {
            *cr = '\0';
        }
        if (parse_wifi_line(line, &aps[n]) == 0) {
            n++;
        }
        if (eol == NULL) {
            break;
        }
        p = eol + 1;
    }

    *count = n;
    return 0;
}

int ml307_network_connect(const pdp_config_t *config)
{
    char cmd[96];

    if (at_exec("AT+CGACT=1,1", 5000) != AT_RESULT_OK) {
        return -1;
    }
    if (config != NULL && config->apn[0] != '\0') {
        str_printf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", config->apn);
        if (at_exec(cmd, 5000) != AT_RESULT_OK) {
            return -1;
        }
    }

    s_connected = true;
    if (ml307_network_is_registered()) {
        s_state = ML307_STATE_CONNECTED;
    } else {
        s_state = ML307_STATE_REGISTERING;
    }
    return 0;
}

int ml307_network_disconnect(void)
{
    (void)at_exec("AT+CGACT=0,1", 3000);
    s_connected = false;
    s_state = ML307_STATE_READY;
    return 0;
}

bool ml307_network_is_registered(void)
{
    const char *resp;

    if (at_exec("AT+CEREG?", 3000) != AT_RESULT_OK) {
        return false;
    }
    resp = at_get_response();
    if (resp == NULL) {
        return false;
    }
    /* 已注册：+CEREG: <n>,1 或 <n>,5 */
    return (strstr(resp, ",1") != NULL) ||
           (strstr(resp, ",5") != NULL);
}

bool ml307_network_is_connected(void)
{
    return s_connected;
}

int ml307_get_ip(char *buffer, uint32_t size)
{
    const char *resp;
    const char *p;

    if (buffer == NULL || size == 0) {
        return -1;
    }
    if (at_exec("AT+CGPADDR=1", 3000) != AT_RESULT_OK) {
        return -1;
    }
    resp = at_get_response();
    p = (resp != NULL) ? strstr(resp, "\"") : NULL;
    if (p != NULL) {
        const char *q = strstr(p + 1, "\"");
        uint32_t len;
        if (q == NULL) {
            return -1;
        }
        len = (uint32_t)(q - p - 1);
        if (len >= size) {
            len = size - 1;
        }
        memcpy(buffer, p + 1, len);
        buffer[len] = '\0';
        return 0;
    }
    return -1;
}