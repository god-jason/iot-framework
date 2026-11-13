/*
 * pc_io.c - 主机平台默认 stdio（stdin / stdout）的 ros_io 实现
 *
 * read 为非阻塞语义：
 *   - 交互终端：有按键才返回，一次返回 1 字节；
 *   - 管道 / 重定向：读到数据或 EOF；EOF 返回 -1，Shell 据此退出。
 * write 直接写 stdout 并立即 flush，保证与 log 输出顺序一致。
 */

#if defined(ROS_PLATFORM_PC)

#include "ros.h"
#include "common.h"

#include <stdio.h>

#if defined(_WIN32)
#include <conio.h>
#include <io.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#endif

/* stdin 是否有数据可读（不阻塞） */
static bool pc_io_stdin_ready(void)
{
#if defined(_WIN32)
    if (_isatty(_fileno(stdin))) {
        return (_kbhit() != 0);
    }
    return true;                /* 管道 / 重定向：read 立即返回数据或 EOF */
#else
    fd_set rfds;
    struct timeval tv;

    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    return (select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) > 0);
#endif
}

static int pc_io_read(ros_io_t *io, void *buf, uint32_t len)
{
    (void)io;
    if (buf == NULL || len == 0U) {
        return -1;
    }
    if (!pc_io_stdin_ready()) {
        return 0;
    }
#if defined(_WIN32)
    if (_isatty(_fileno(stdin))) {
        int c = _getch();

        if (c == 0 || c == 0xE0) {
            (void)_getch();     /* 方向键 / 功能键：吞掉扫描码 */
            return 0;
        }
        ((unsigned char *)buf)[0] = (unsigned char)c;
        return 1;
    }
    {
        int n = _read(_fileno(stdin), buf, (unsigned int)len);

        return (n > 0) ? n : -1;        /* 0 = EOF */
    }
#else
    {
        static bool nonblock;
        ssize_t n;

        if (!nonblock) {
            /* 管道在无数据时会阻塞，置为非阻塞避免卡住协作式调度器 */
            (void)fcntl(STDIN_FILENO, F_SETFL,
                        fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);
            nonblock = true;
        }
        n = read(STDIN_FILENO, buf, (size_t)len);
        if (n > 0) {
            return (int)n;
        }
        if (n == 0) {
            return -1;              /* EOF */
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }
#endif
}

static int pc_io_write(ros_io_t *io, const void *buf, uint32_t len)
{
    (void)io;
    if (buf == NULL) {
        return -1;
    }
    if (len == 0U) {
        return 0;
    }
    if (fwrite(buf, 1U, (size_t)len, stdout) != (size_t)len) {
        return -1;
    }
    (void)fflush(stdout);
    return (int)len;
}

static int pc_io_close(ros_io_t *io)
{
    (void)io;
    (void)fflush(stdout);
    return 0;
}

ros_io_t *ros_io_platform_default(void)
{
    static ros_io_t io;

    io.read = pc_io_read;
    io.write = pc_io_write;
    io.close = pc_io_close;
    io.user = NULL;
    return &io;
}

/* ros_init() 调用：把主机标准输入输出注册为默认 stdio */
void ros_io_platform_init(void)
{
    ros_io_set_default(ros_io_platform_default());
}

#endif /* ROS_PLATFORM_PC */
