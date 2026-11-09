/*
 * syscalls.c - newlib 系统调用桩（裸机固件）
 *
 * 内核日志走 printf，因此 newlib 需要一组 syscall 实现。
 * 这里把 stdout 输出直接丢弃（不占用 UART），堆由 _sbrk 在
 * .bss 之后到主栈预留区之间分配，避免引入半主机依赖。
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

extern char end[];        /* 链接脚本 PROVIDE(end)：.noinit 之后的堆起点 */
extern char _estack[];

/* 主栈预留 2KB，与链接脚本 _Min_Stack_Size 一致 */
#define BOARD_STACK_RESERVE 0x800U

void *_sbrk(ptrdiff_t incr)
{
    static char *heap_cur;
    char *prev;
    char *limit = (char *)((uintptr_t)_estack - BOARD_STACK_RESERVE);

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
    (void)fd;
    (void)buf;
    return (int)count;    /* 丢弃输出 */
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
