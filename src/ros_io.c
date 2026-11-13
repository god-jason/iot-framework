/*
 * ros_io.c - 标准输入输出抽象
 *
 * 默认 stdio 的解析顺序：
 *   当前任务绑定的 io → 全局默认 io（平台在 ros_init 中初始化）
 *   → 内置 null io（丢弃输出、无输入）。
 */

#include "ros_io.h"
#include "ros_task.h"
#include "ros_sched.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ros_io_t *s_default_io;

static int null_read(ros_io_t *io, void *buf, uint32_t len)
{
    (void)io;
    (void)buf;
    (void)len;
    return 0;
}

static int null_write(ros_io_t *io, const void *buf, uint32_t len)
{
    (void)io;
    (void)buf;
    return (int)len;
}

static int null_close(ros_io_t *io)
{
    (void)io;
    return 0;
}

static ros_io_t s_null_io = { null_read, null_write, null_close, NULL };

void ros_io_set_default(ros_io_t *io)
{
    s_default_io = io;
}

ros_io_t *ros_io_default(void)
{
    return (s_default_io != NULL) ? s_default_io : &s_null_io;
}

ros_io_t *ros_stdio(void)
{
    if (ros_current_task != NULL && ros_current_task->stdio != NULL) {
        return ros_current_task->stdio;
    }
    return ros_io_default();
}

int ros_stdio_set(ros_io_t *io)
{
    if (ros_current_task == NULL) {
        return -1;
    }
    ros_current_task->stdio = (io != NULL) ? io : ros_io_default();
    return 0;
}

int ros_io_read(ros_io_t *io, void *buf, uint32_t len)
{
    if (io == NULL) {
        io = ros_stdio();
    }
    if (io->read == NULL) {
        return -1;
    }
    return io->read(io, buf, len);
}

int ros_io_write(ros_io_t *io, const void *buf, uint32_t len)
{
    if (io == NULL) {
        io = ros_stdio();
    }
    if (io->write == NULL) {
        return -1;
    }
    return io->write(io, buf, len);
}

int ros_io_puts(ros_io_t *io, const char *s)
{
    if (s == NULL) {
        return -1;
    }
    return ros_io_write(io, s, (uint32_t)strlen(s));
}

int ros_io_close(ros_io_t *io)
{
    if (io == NULL) {
        io = ros_stdio();
    }
    if (io->close == NULL) {
        return 0;
    }
    return io->close(io);
}

ros_io_t *ros_io_null(void)
{
    return &s_null_io;
}

/* ============================== stdio 风格接口 ============================== */

/* 从 io 读一个字节；wait 为 true 时无数据则让出 CPU 等待 */
static int io_read_char(ros_io_t *io, bool wait)
{
    unsigned char c;

    for (;;) {
        int n;

        if (io->read == NULL) {
            return -1;
        }
        n = io->read(io, &c, 1U);
        if (n > 0) {
            return (int)c;
        }
        if (n < 0) {
            return -1;
        }
        if (!wait || ros_current_task == NULL) {
            return -1;              /* 非阻塞 / 无任务上下文：视为无数据 */
        }
        ros_sleep_ms(1U);
    }
}

int ros_putchar(int c)
{
    unsigned char ch = (unsigned char)c;

    return ros_io_write(NULL, &ch, 1U);
}

int ros_puts(const char *s)
{
    int w;

    if (s == NULL) {
        return -1;
    }
    w = ros_io_write(NULL, s, (uint32_t)strlen(s));
    if (w < 0) {
        return -1;
    }
    if (ros_io_write(NULL, "\n", 1U) < 0) {
        return -1;
    }
    return w + 1;
}

int ros_printf(const char *fmt, ...)
{
    char buf[ROS_IO_PRINT_MAX];
    va_list ap;
    int n;

    if (fmt == NULL) {
        return -1;
    }
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) {
        return -1;
    }
    if ((uint32_t)n >= sizeof(buf)) {
        n = (int)sizeof(buf) - 1;   /* 超长：输出截断部分 */
    }
    return ros_io_write(NULL, buf, (uint32_t)n);
}

int ros_getchar(void)
{
    return io_read_char(ros_stdio(), true);
}

int ros_gets(char *buf, uint32_t cap)
{
    ros_io_t *io = ros_stdio();
    uint32_t n = 0U;

    if (buf == NULL || cap == 0U || io->read == NULL) {
        return -1;
    }
    for (;;) {
        int c = io_read_char(io, true);

        if (c < 0) {
            if (n == 0U) {
                return -1;      /* 输入结束 */
            }
            break;
        }
        if (c == '\n') {
            break;
        }
        if (c == '\r') {
            continue;
        }
        if (n + 1U < cap) {
            buf[n++] = (char)c;
        }
    }
    buf[n] = '\0';
    return (int)n;
}

int ros_scanf(const char *fmt, ...)
{
    char line[ROS_IO_LINE_MAX];
    const char *f;
    const char *p;
    va_list ap;
    int assigned = 0;

    if (fmt == NULL || ros_gets(line, sizeof(line)) < 0) {
        return -1;
    }

    f = fmt;
    p = line;
    va_start(ap, fmt);
    while (*f != '\0') {
        if (*f == ' ' || *f == '\t') {
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            f++;
            continue;
        }
        if (*f == '%') {
            bool is_c;
            bool is_s;

            f++;
            if (*f == '%') {
                if (*p != '%') {
                    break;
                }
                p++;
                f++;
                continue;
            }
            is_c = (*f == 'c');
            is_s = (*f == 's');
            if (!is_c && !is_s && *f != 'd' && *f != 'u' &&
                *f != 'x' && *f != 'X') {
                break;              /* 不支持的类型：停止解析 */
            }
            if (!is_c) {
                while (*p == ' ' || *p == '\t') {
                    p++;
                }
            }
            if (is_c) {
                if (*p == '\0') {
                    break;
                }
                *(va_arg(ap, char *)) = *p++;
            } else if (is_s) {
                char *dst = va_arg(ap, char *);
                int n = 0;

                while (*p != '\0' && *p != ' ' && *p != '\t') {
                    dst[n++] = *p++;
                }
                dst[n] = '\0';
                if (n == 0) {
                    break;
                }
            } else {
                char *end = NULL;
                long v;

                if (*p == '\0') {
                    break;
                }
                v = (*f == 'd') ? strtol(p, &end, 10)
                                : (long)strtoul(p, &end, (*f == 'x' || *f == 'X')
                                                       ? 16 : 10);
                if (end == p) {
                    break;
                }
                if (*f == 'd') {
                    *(va_arg(ap, int *)) = (int)v;
                } else {
                    *(va_arg(ap, unsigned int *)) = (unsigned int)v;
                }
                p = end;
            }
            assigned++;
            f++;
            continue;
        }

        /* 普通字符：必须与输入一致 */
        if (*p != *f) {
            break;
        }
        p++;
        f++;
    }
    va_end(ap);
    return assigned;
}
