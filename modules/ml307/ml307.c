#include "ml307.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    if (resp != NULL && sscanf(resp, "+CSQ: %d", &rssi) == 1) {
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
    if (resp == NULL || sscanf(resp, "+CSQ: %d,%d", &c, &b) < 1) {
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

    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn);
    if (at_exec(cmd, 5000) != AT_RESULT_OK) {
        return -1;
    }

    if ((username != NULL && username[0] != '\0') ||
        (password != NULL && password[0] != '\0')) {
        snprintf(cmd, sizeof(cmd), "AT+CGAUTH=1,1,\"%s\",\"%s\"",
                 (username != NULL) ? username : "",
                 (password != NULL) ? password : "");
        if (at_exec(cmd, 5000) != AT_RESULT_OK) {
            return -1;
        }
    }
    return 0;
}

/* 读取一个逗号分隔字段，支持可选的成对引号 */
static const char *wifi_next_field(const char *p, char *out, size_t cap)
{
    size_t n = 0U;
    bool quoted = false;

    while (*p == ' ' || *p == '\t') {
        p++;
    }
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
        snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", config->apn);
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