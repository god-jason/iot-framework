#include "ml307.h"
#include "iot_string.h"

#include <stdio.h>
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
    return IOT_OK;
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

    if (at_start() != IOT_OK) {
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
    return IOT_OK;
}

int ml307_stop(void)
{
    at_stop();
    s_state = ML307_STATE_OFF;
    s_connected = false;
    return IOT_OK;
}

int ml307_reset(void)
{
    (void)at_exec("AT+RESET", 3000);
    s_state = ML307_STATE_OFF;
    s_connected = false;
    return IOT_OK;
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
        return IOT_OK;
    }
    return -1;
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
    return IOT_OK;
}

int ml307_network_disconnect(void)
{
    (void)at_exec("AT+CGACT=0,1", 3000);
    s_connected = false;
    s_state = ML307_STATE_READY;
    return IOT_OK;
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
    return (iot_string_find(resp, ",1") != NULL) ||
           (iot_string_find(resp, ",5") != NULL);
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
    p = (resp != NULL) ? iot_string_find(resp, "\"") : NULL;
    if (p != NULL) {
        const char *q = iot_string_find(p + 1, "\"");
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
        return IOT_OK;
    }
    return -1;
}