/*
 * ml307_http.c - ML307R 原生 HTTP 实现（AT+MHTTP*），平替 src/http
 *
 * 启用方式：ML307 目标的构建系统编译本文件，并排除 src/http/http_client.c。
 * 使用模组内置 HTTP 协议栈，不经过 net socket，节省 RAM 与交互次数。
 *
 * 指令序列（HTTP/HTTPS 用户手册 V6.0.7，实例 id 固定 0）：
 *   AT+MHTTPCREATE="http://<host>[:port]"      -> +MHTTPCREATE: <httpid>
 *   AT+MHTTPCFG="cached",<id>,1                 缓存模式（本文件依赖它读取响应）
 *   AT+MHTTPHEADER=<id>,0,0,"<name>: <value>"   单次请求报头
 *   AT+MHTTPCONTENT=<id>,0,<len>                -> '>' -> 正文 -> OK（POST）
 *   AT+MHTTPREQUEST=<id>,<method>,0,"<path>"    method: 1 GET, 2 POST
 *   +MHTTPURC: "recv",<id>,<code>,<header_len>,<content_len>
 *   +MHTTPURC: "err",<id>,<error_code>
 *   AT+MHTTPREAD=<id>,0|1,<len>                 0 读报头，1 读正文
 *   AT+MHTTPDEL=<id>                            删除实例
 *
 * 限制：不支持 HTTPS；响应走模组缓存（上限 8K，超出的页面报 +MHTTPURC: "err"）；
 *       正文按文本行分块读取，CR/LF 会被规范成 '\n'，单行受 AT 行缓冲（224B）限制。
 */

#include "http.h"
#include "ml307.h"
#include "at.h"
#include "at_urc.h"
#include "hal_def.h"
#include "ros.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ML307_HTTP_URC              "+MHTTPURC:"
#define ML307_HTTP_DEFAULT_MS       30000U
#define ML307_HTTP_CMD_MAX          368U
#define ML307_HTTP_CREATE_MAX       192U
#define ML307_HTTP_READ_TIMEOUT     150U    /* 收 AT+MHTTPREAD 响应的短超时 */
#define ML307_HTTP_READ_CHUNK       120U
#define ML307_HTTP_CONTENT_TYPE_MAX 47U

/* AT 命令缓冲：静态分配，避免占用任务栈（模组 AT 引擎为全局单例） */
static char s_cmd[ML307_HTTP_CMD_MAX];

static bool s_http_urc;
static int s_http_id = -1;
static volatile bool s_recv_done;
static volatile bool s_recv_err;
static volatile int s_recv_code;
static volatile int s_recv_len;

/* ---------------- URC ---------------- */

/* 跳过分隔符并解析一个十进制字段，返回字段之后的位置 */
static const char *ml307_http_int(const char *p, int *out)
{
    int v = 0;

    while (*p == ' ' || *p == '\t' || *p == ',') {
        p++;
    }
    if (*p < '0' || *p > '9') {
        *out = 0;
        return p;
    }
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
    }
    *out = v;
    if (*p == ',') {
        p++;
    }
    return p;
}

/* +MHTTPURC: "recv",<id>,<code>,<header_len>,<content_len> 或 "err",<id>,<code> */
static void ml307_http_on_urc(const char *line, uint32_t len, void *user)
{
    const char *p;
    const char *end = line + len;

    (void)user;
    if (line == NULL || len < 11U || !str_starts_with(line, ML307_HTTP_URC)) {
        return;
    }
    p = line + 10;
    while (p < end && (*p == ' ' || *p == '\t')) {
        p++;
    }
    if (p >= end || *p != '"') {
        return;
    }
    p++;

    if (strncmp(p, "recv\"", 5U) == 0) {
        int id;
        int code;
        int hdr_len;
        int content_len;

        p += 5;
        p = ml307_http_int(p, &id);
        p = ml307_http_int(p, &code);
        p = ml307_http_int(p, &hdr_len);
        p = ml307_http_int(p, &content_len);
        if (id != s_http_id) {
            return;
        }
        (void)hdr_len;   /* 报头长度用不到 */
        s_recv_code = code;
        s_recv_len = content_len;
        s_recv_done = true;
    } else if (strncmp(p, "err\"", 4U) == 0) {
        int id;
        int code;

        p += 4;
        p = ml307_http_int(p, &id);
        p = ml307_http_int(p, &code);
        if (id != s_http_id) {
            return;
        }
        (void)code;
        s_recv_err = true;
        s_recv_done = true;
    }
}

static int ml307_http_ensure_urc(void)
{
    if (!s_http_urc) {
        if (at_urc_register(ML307_HTTP_URC, ml307_http_on_urc, NULL) != 0) {
            return NET_ERR_NOMEM;
        }
        s_http_urc = true;
    }
    return NET_OK;
}

/* ---------------- 实例与请求 ---------------- */

/* 创建实例，返回实例 id（0~3）或负值错误码 */
static int ml307_http_create(const http_url_t *u)
{
    static char host[ML307_HTTP_CREATE_MAX];
    const char *resp;
    const char *p;
    int id;
    int n;

    if (u->port == 80U) {
        n = str_printf(host, sizeof(host), "http://%s", u->host);
    } else {
        n = str_printf(host, sizeof(host), "http://%s:%u", u->host,
                     (unsigned)u->port);
    }
    if (n <= 0 || n >= (int)sizeof(host)) {
        return NET_ERR_PARAM;
    }
    /* 模组可能残留上次会话的实例，先删掉 */
    (void)at_exec("AT+MHTTPDEL=0", 3000U);

    if (str_printf(s_cmd, sizeof(s_cmd), "AT+MHTTPCREATE=\"%s\"", host) <= 0) {
        return NET_ERR_PARAM;
    }
    if (at_exec(s_cmd, 10000U) != AT_RESULT_OK) {
        return NET_ERR_IO;
    }

    resp = at_get_response();
    p = (resp != NULL) ? strstr(resp, "+MHTTPCREATE:") : NULL;
    if (p == NULL) {
        return NET_ERR_IO;
    }
    p += 13;
    id = (int)strtol(p, NULL, 10);
    if (id < 0 || id > 3) {
        return NET_ERR_IO;
    }
    return id;
}

/* 设置单次请求报头；name/value 不能含引号或换行 */
static int ml307_http_header(int id, const char *name, const char *value)
{
    int n;

    if (name == NULL || value == NULL || name[0] == '\0' ||
        strpbrk(name, "\"\r\n") != NULL || strpbrk(value, "\"\r\n") != NULL) {
        return NET_ERR_PARAM;
    }
    n = str_printf(s_cmd, sizeof(s_cmd), "AT+MHTTPHEADER=%d,0,0,\"%s: %s\"",
                   id, name, value);
    if (n <= 0 || n >= (int)sizeof(s_cmd)) {
        return NET_ERR_PARAM;
    }
    return (at_exec(s_cmd, 5000U) == AT_RESULT_OK) ? NET_OK : NET_ERR_IO;
}

/* POST 正文：AT+MHTTPCONTENT 进入 '>' 数据模式 */
static int ml307_http_content(int id, const void *body, uint32_t len)
{
    at_request_t phase;

    if (str_printf(s_cmd, sizeof(s_cmd), "AT+MHTTPCONTENT=%d,0,%u",
                   id, (unsigned)len) <= 0) {
        return NET_ERR_PARAM;
    }

    memset(&phase, 0, sizeof(phase));
    phase.command = s_cmd;
    phase.success = ">";
    phase.failure = "ERROR";
    phase.timeout = 10000U;
    if (at_transact(&phase) != AT_RESULT_OK) {
        return NET_ERR_IO;
    }
    if (at_send_raw((const uint8_t *)body, len) != 0) {
        return NET_ERR_IO;
    }
    if (at_exec(NULL, 10000U) != AT_RESULT_OK) {
        return NET_ERR_IO;
    }
    return NET_OK;
}

/*
 * 收集一条命令的完整响应。
 * 不能用 at_exec：MHTTPREAD 的正文里可能含 "OK"/"ERROR"，会让事务提前
 * 结束并丢掉后面的行。这里把成功/失败终止符设为不会出现的控制字符，
 * 靠短超时收完整个响应。
 */
static void ml307_http_collect(uint32_t timeout_ms)
{
    at_request_t req;

    memset(&req, 0, sizeof(req));
    req.command = s_cmd;
    req.success = "\x01";
    req.failure = "\x02";
    req.timeout = timeout_ms;
    (void)at_transact(&req);
}

/*
 * 读取缓存数据。
 * 返回 "+MHTTPREAD:" 行之后的正文起点；*unread 为剩余未读长度，
 * *data_len 为本包数据长度。失败返回 NULL。
 */
static const char *ml307_http_read(int id, int type, uint32_t read_len,
                                   int *unread, int *data_len)
{
    const char *resp;
    const char *p;
    int fid;
    int ftype;
    int un;
    int dl;

    if (str_printf(s_cmd, sizeof(s_cmd), "AT+MHTTPREAD=%d,%d,%u",
                   id, type, (unsigned)read_len) <= 0) {
        return NULL;
    }
    ml307_http_collect(ML307_HTTP_READ_TIMEOUT);

    resp = at_get_response();
    p = (resp != NULL) ? strstr(resp, "+MHTTPREAD:") : NULL;
    if (p == NULL) {
        return NULL;
    }
    p += 11;
    p = ml307_http_int(p, &fid);
    p = ml307_http_int(p, &ftype);
    p = ml307_http_int(p, &un);
    p = ml307_http_int(p, &dl);
    if (fid != id || ftype != type) {
        return NULL;
    }
    if (unread != NULL) {
        *unread = un;
    }
    if (data_len != NULL) {
        *data_len = dl;
    }
    return p;
}

/* 从报头文本中提取 Content-Type（大小写不敏感），找不到保持为空 */
static void ml307_http_parse_content_type(const char *text, char *out,
                                          uint32_t size)
{
    static const char *const keys[] = { "Content-Type:", "content-type:" };
    uint32_t k;

    for (k = 0U; k < 2U; k++) {
        const char *p = strstr(text, keys[k]);
        const char *v;
        uint32_t n = 0U;

        if (p == NULL) {
            continue;
        }
        v = p + strlen(keys[k]);
        while (*v == ' ' || *v == '\t') {
            v++;
        }
        while (v[n] != '\0' && v[n] != '\r' && v[n] != '\n' &&
               n < size) {
            out[n] = v[n];
            n++;
        }
        out[n] = '\0';
        return;
    }
}

/* 分块读取正文并回调，避免单行超过 AT 行缓冲 */
static void ml307_http_emit(int id, http_body_cb cb, void *user)
{
    uint32_t guard = 0U;

    while (guard++ < 512U) {
        const char *data;
        int unread = 0;
        int data_len = 0;
        uint32_t avail;
        uint32_t n;

        data = ml307_http_read(id, 1, ML307_HTTP_READ_CHUNK, &unread,
                               &data_len);
        if (data == NULL) {
            return;
        }
        avail = (uint32_t)strlen(data);
        n = ((uint32_t)data_len < avail) ? (uint32_t)data_len : avail;
        if (n > 0U && cb((const uint8_t *)data, n, user) != 0) {
            return;   /* 调用者主动中止 */
        }
        if (unread <= 0 || n == 0U) {
            return;
        }
    }
}

static int ml307_http_request(const char *method, const char *url,
                              const http_header_t *headers,
                              uint32_t header_count,
                              const char *content_type, const void *body,
                              uint32_t body_len, http_response_t *rsp,
                              http_body_cb cb, void *user,
                              uint32_t timeout_ms)
{
    http_url_t u;
    uint32_t wait = (timeout_ms != 0U) ? timeout_ms : ML307_HTTP_DEFAULT_MS;
    uint32_t start;
    uint32_t i;
    bool is_post;
    int method_id;
    int id;
    int rc;

    if (method == NULL || http_url_parse(url, &u) != NET_OK) {
        return NET_ERR_PARAM;
    }
    if (u.https) {
        return NET_ERR_NOT_SUPPORTED;
    }
    if ((body == NULL && body_len != 0U) ||
        (headers == NULL && header_count != 0U)) {
        return NET_ERR_PARAM;
    }
    is_post = (strcmp(method, "POST") == 0);
    method_id = is_post ? 2 : 1;
    if (rsp != NULL) {
        memset(rsp, 0, sizeof(*rsp));
    }
    rc = ml307_http_ensure_urc();
    if (rc != NET_OK) {
        return rc;
    }

    rc = ml307_http_create(&u);
    if (rc < 0) {
        return rc;
    }
    id = rc;
    s_http_id = id;

    /* 缓存模式：请求完成后 +MHTTPURC: "recv" 上报状态与长度 */
    if (str_printf(s_cmd, sizeof(s_cmd), "AT+MHTTPCFG=\"cached\",%d,1,8192",
                   id) <= 0 ||
        at_exec(s_cmd, 5000U) != AT_RESULT_OK) {
        goto fail;
    }

    if (is_post) {
        rc = ml307_http_header(id, "Content-Type",
                               (content_type != NULL) ? content_type
                                                      : "application/octet-stream");
        if (rc != NET_OK) {
            goto fail;
        }
    }
    for (i = 0U; i < header_count; i++) {
        rc = ml307_http_header(id, headers[i].name, headers[i].value);
        if (rc != NET_OK) {
            goto fail;
        }
    }
    if (is_post && body_len != 0U && ml307_http_content(id, body, body_len) != NET_OK) {
        goto fail;
    }

    s_recv_done = false;
    s_recv_err = false;
    s_recv_code = 0;
    s_recv_len = 0;

    if (str_printf(s_cmd, sizeof(s_cmd), "AT+MHTTPREQUEST=%d,%d,0,\"%s\"",
                   id, method_id, u.path) <= 0 ||
        at_exec(s_cmd, 5000U) != AT_RESULT_OK) {
        goto fail;
    }

    start = hal_tick_get();
    while (!s_recv_done && (uint32_t)(hal_tick_get() - start) < wait) {
        (void)at_process();
        ros_sleep_ms(10U);
    }
    if (!s_recv_done || s_recv_err) {
        goto fail;
    }
    if (rsp != NULL) {
        rsp->status = s_recv_code;
        rsp->content_length = (s_recv_len > 0) ? (uint32_t)s_recv_len : 0U;
    }

    /* 报头缓存一次读出，取 Content-Type（长度可视需要再拆包） */
    {
        const char *hdr;
        int unread = 0;
        int data_len = 0;

        hdr = ml307_http_read(id, 0, 0U, &unread, &data_len);
        if (hdr != NULL && rsp != NULL) {
            ml307_http_parse_content_type(hdr, rsp->content_type,
                                          ML307_HTTP_CONTENT_TYPE_MAX);
        }
    }

    if (cb != NULL && s_recv_len > 0) {
        ml307_http_emit(id, cb, user);
    }

    (void)str_printf(s_cmd, sizeof(s_cmd), "AT+MHTTPDEL=%d", id);
    (void)at_exec(s_cmd, 3000U);
    s_http_id = -1;
    return NET_OK;

fail:
    (void)str_printf(s_cmd, sizeof(s_cmd), "AT+MHTTPTERM=%d", id);
    (void)at_exec(s_cmd, 3000U);
    (void)str_printf(s_cmd, sizeof(s_cmd), "AT+MHTTPDEL=%d", id);
    (void)at_exec(s_cmd, 3000U);
    s_http_id = -1;
    return NET_ERR_IO;
}

int http_get(const char *url, const http_header_t *headers,
             uint32_t header_count, http_response_t *rsp, http_body_cb cb,
             void *user, uint32_t timeout_ms)
{
    return ml307_http_request("GET", url, headers, header_count, NULL, NULL,
                              0U, rsp, cb, user, timeout_ms);
}

int http_post(const char *url, const http_header_t *headers,
              uint32_t header_count, const char *content_type,
              const void *body, uint32_t body_len,
              http_response_t *rsp, http_body_cb cb, void *user,
              uint32_t timeout_ms)
{
    if (body == NULL && body_len != 0U) {
        return NET_ERR_PARAM;
    }
    return ml307_http_request("POST", url, headers, header_count,
                              content_type, body, body_len, rsp, cb, user,
                              timeout_ms);
}

typedef struct {
    FILE *fp;
} ml307_http_file_t;

static int ml307_http_file_write(const uint8_t *data, uint32_t len, void *user)
{
    ml307_http_file_t *f = (ml307_http_file_t *)user;

    if (f == NULL || f->fp == NULL) {
        return -1;
    }
    return (fwrite(data, 1U, len, f->fp) == len) ? 0 : -1;
}

int http_download(const char *url, const http_header_t *headers,
                  uint32_t header_count, const char *path,
                  uint32_t timeout_ms)
{
    ml307_http_file_t f;
    http_response_t rsp;
    int rc;

    if (path == NULL) {
        return NET_ERR_PARAM;
    }
    f.fp = fopen(path, "wb");
    if (f.fp == NULL) {
        return NET_ERR_IO;
    }
    rc = http_get(url, headers, header_count, &rsp, ml307_http_file_write,
                  &f, timeout_ms);
    (void)fclose(f.fp);
    if (rc != NET_OK || rsp.status < 200 || rsp.status >= 300) {
        (void)remove(path);
        return (rc != NET_OK) ? rc : NET_ERR_IO;
    }
    return NET_OK;
}
