/*
 * pc_trace.c - 主机平台 trace 文件输出
 *
 * 事件由 ros_trace.c 格式化成文本行，这里写入文件：
 *   - 未调用 ros_trace_start_file() 时默认写入 ROS_TRACE_DEFAULT_FILE；
 *   - 每行立即 fflush，进程崩溃时已输出内容不丢。
 * 全程无内存快照、无缓冲积累。
 */

#if defined(ROS_PLATFORM_PC)

#include "ros.h"
#include "ros_trace.h"

#include <stdio.h>

#ifndef ROS_TRACE_DEFAULT_FILE
#define ROS_TRACE_DEFAULT_FILE "ros_trace.log"
#endif

/* ------------------------------ 文本行文件 ------------------------------ */

static FILE *s_trace_file;
static int   s_file_resolved;       /* 已尝试打开（含默认文件） */

/* PC 端输出固定走文件，handle 参数忽略 */
void ros_trace_set_output(void *handle, uint32_t timeout_ms)
{
    (void)handle;
    (void)timeout_ms;
}

/* 取当前输出文件；未指定时惰性打开默认日志文件 */
static FILE *ros_trace_stream(void)
{
    if ((s_trace_file == NULL) && !s_file_resolved) {
        s_file_resolved = 1;
        s_trace_file = fopen(ROS_TRACE_DEFAULT_FILE, "wb");
    }
    return s_trace_file;
}

void ros_port_trace_write(const char *data, uint32_t len)
{
    FILE *out = ros_trace_stream();

    if ((out == NULL) || (data == NULL) || (len == 0U)) {
        return;
    }
    (void)fwrite(data, 1U, (size_t)len, out);
    (void)fflush(out);
}

void ros_trace_start_file(const char *path)
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

void ros_trace_stop_file(void)
{
    if (s_trace_file != NULL) {
        (void)fclose(s_trace_file);
        s_trace_file = NULL;
    }
    s_file_resolved = 1;        /* 显式停止后不再回落到默认文件 */
}

#endif /* ROS_PLATFORM_PC */
