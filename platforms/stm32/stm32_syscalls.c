/*
 * stm32_syscalls.c - newlib 系统调用桩（STM32 裸机，F1/F4 共用）
 *
 * 内核日志走 printf：_write 把 stdout 重定向到板级控制台串口
 * （与 Shell 同一路），堆由 _sbrk 在 .bss 之后到主栈预留区之间分配，
 * 避免引入半主机依赖。
 *
 * 控制台句柄遵循平台约定：板级提供 board_shell_uart() 时使用其返回的
 * 句柄；未提供（F1 寄存器串口）时用非空句柄 1 表示 UART1。
 */

#include "hal_uart.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

extern char end[];        /* 链接脚本 PROVIDE(end)：.noinit 之后的堆起点 */
extern char _estack[];

/* 链接脚本提供的堆上限（end + _Min_Heap_Size）；未提供时退回主栈预留估算 */
extern char __heap_limit[] __attribute__((weak));

/* 兜底：主栈预留区。各系列 _Min_Stack_Size 不同，优先使用 __heap_limit */
#define BOARD_STACK_RESERVE 0x800U

void *_sbrk(ptrdiff_t incr)
{
    static char *heap_cur;
    char *prev;
    char *limit = ((uintptr_t)__heap_limit != 0U)
                      ? __heap_limit
                      : (char *)((uintptr_t)_estack - BOARD_STACK_RESERVE);

    if (heap_cur == NULL) {
        /* 从 end 开始，避开 .noinit（崩溃记录必须跨复位保留） */
        heap_cur = end;
    }

    prev = heap_cur;
    if ((incr > 0 && heap_cur + incr > limit) ||
        (incr < 0 && heap_cur + incr < end)) {
        errno = ENOMEM;
        return (void *)-1;
    }

    heap_cur += incr;
    return (void *)prev;
}

int _write(int fd, const void *buf, size_t count)
{
    hal_uart_t *handle;

    (void)fd;
    if (buf == NULL || count == 0U) {
        return 0;
    }
    /* 与 Shell 共用控制台串口；未初始化时返回错误，日志自然丢弃 */
    handle = (board_shell_uart != NULL) ? board_shell_uart() : NULL;
    if (handle == NULL) {
        handle = (hal_uart_t *)(void *)1;
    }
    (void)hal_uart_transmit(handle, (const uint8_t *)buf, count, 100U);
    return (int)count;
}

int _read(int fd, void *buf, size_t count)
{
    (void)fd;
    (void)buf;
    (void)count;
    return 0;             /* EOF */
}

int _close(int fd)
{
    (void)fd;
    return -1;
}

int _fstat(int fd, struct stat *st)
{
    (void)fd;
    if (st != NULL) {
        st->st_mode = S_IFCHR;
    }
    return 0;
}

int _isatty(int fd)
{
    (void)fd;
    return 1;
}

off_t _lseek(int fd, off_t offset, int whence)
{
    (void)fd;
    (void)offset;
    (void)whence;
    return 0;
}

int _open(const char *path, int flags, ...)
{
    (void)path;
    (void)flags;
    return -1;
}

void _exit(int status)
{
    (void)status;
    for (;;) {
    }
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

int _getpid(void)
{
    return 1;
}
