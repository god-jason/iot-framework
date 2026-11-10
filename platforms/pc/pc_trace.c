/*
 * pc_trace.c - 主机平台 trace 文件输出
 *
 * 事件由 iot_trace.c 格式化成文本行，这里写入文件：
 *   - 未调用 iot_trace_start_file() 时默认写入 IOT_TRACE_DEFAULT_FILE；
 *   - 每行立即 fflush，进程崩溃时已输出内容不丢。
 * 另提供可选 CTF 文件输出（二进制流 + metadata），与文本行由同一事件源驱动。
 * 全程无内存快照、无缓冲积累。
 */

#if defined(IOT_PLATFORM_PC)

#include "iot.h"
#include "iot_trace.h"

#if IOT_TRACE_BACKEND_BARECTF
#include "barectf.h"
#include "metadata.h"
#endif

#include <stdio.h>
#include <string.h>

#ifndef IOT_TRACE_DEFAULT_FILE
#define IOT_TRACE_DEFAULT_FILE "iot_trace.log"
#endif

/* ------------------------------ 文本行文件 ------------------------------ */

static FILE *s_trace_file;
static int   s_file_resolved;       /* 已尝试打开（含默认文件） */

/* PC 端输出固定走文件，handle 参数忽略 */
void iot_trace_set_output(void *handle, uint32_t timeout_ms)
{
    (void)handle;
    (void)timeout_ms;
}

/* 取当前输出文件；未指定时惰性打开默认日志文件 */
static FILE *iot_trace_stream(void)
{
    if ((s_trace_file == NULL) && !s_file_resolved) {
        s_file_resolved = 1;
        s_trace_file = fopen(IOT_TRACE_DEFAULT_FILE, "wb");
    }
    return s_trace_file;
}

void iot_port_trace_write(const char *data, uint32_t len)
{
    FILE *out = iot_trace_stream();

    if ((out == NULL) || (data == NULL) || (len == 0U)) {
        return;
    }
    (void)fwrite(data, 1U, (size_t)len, out);
    (void)fflush(out);
}

void iot_trace_start_file(const char *path)
{
    if (s_trace_file != NULL) {
        (void)fclose(s_trace_file);
        s_trace_file = NULL;
    }
    s_file_resolved = (path != NULL);
    if (path != NULL) {
        s_trace_file = fopen(path, "wb");
    }
}

void iot_trace_stop_file(void)
{
    if (s_trace_file != NULL) {
        (void)fclose(s_trace_file);
        s_trace_file = NULL;
    }
    s_file_resolved = 1;        /* 显式停止后不再回落到默认文件 */
}

/* ------------------------------ CTF 文件 ------------------------------ */

#if IOT_TRACE_BACKEND_BARECTF

static FILE *s_ctf_file;

/* barectf 关包回调：把整包 CTF 二进制写入流文件 */
static void iot_trace_ctf_sink(const uint8_t *data, uint32_t len, void *user)
{
    (void)user;
    if (data == NULL) {
        return;
    }
    if (s_ctf_file != NULL) {
        (void)fwrite(data, 1U, (size_t)len, s_ctf_file);
        (void)fflush(s_ctf_file);
    }
}

void iot_trace_start_ctf_file(const char *path)
{
    char meta[1024];
    FILE *mf;
    size_t n;

    if (path == NULL) {
        return;
    }

    /* 先收尾上一个 CTF 文件 */
    if (s_ctf_file != NULL) {
        (void)iot_trace_barectf_flush();
        (void)fclose(s_ctf_file);
        s_ctf_file = NULL;
    }

    s_ctf_file = fopen(path, "wb");
    if (s_ctf_file == NULL) {
        return;
    }

    /* 同目录写出 CTF metadata（babeltrace2 需要 metadata + 二进制流） */
    n = (size_t)strlen(path);
    if (n >= sizeof(meta) - 10U) {
        n = sizeof(meta) - 10U;
    }
    (void)memcpy(meta, path, n);
    (void)strcpy(meta + n, ".metadata");
    mf = fopen(meta, "wb");
    if (mf != NULL) {
        (void)fwrite(g_iot_barectf_metadata, 1U,
                     strlen(g_iot_barectf_metadata), mf);
        (void)fclose(mf);
    }

    iot_trace_barectf_set_sink(iot_trace_ctf_sink, NULL);
}

void iot_trace_stop_ctf_file(void)
{
    if (s_ctf_file != NULL) {
        (void)iot_trace_barectf_flush();   /* 关闭最后一个 packet 并写出 */
        (void)fclose(s_ctf_file);
        s_ctf_file = NULL;
    }
    iot_trace_barectf_set_sink(NULL, NULL);
}

#else /* 未启用 CTF 后端：提供空实现，保证链接完整 */

void iot_trace_start_ctf_file(const char *path) { (void)path; }
void iot_trace_stop_ctf_file(void) {}

#endif /* IOT_TRACE_BACKEND_BARECTF */

#endif /* IOT_PLATFORM_PC */
