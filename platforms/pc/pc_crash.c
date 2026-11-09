/*
 * pc_crash.c - PC 模拟端的致命异常捕获
 *
 * Windows：SetUnhandledExceptionFilter 捕获未处理 SEH 异常
 *          （访问违例、栈溢出、除零等），记录异常码与异常地址后终止。
 * POSIX  ：sigaction(SA_SIGINFO) 捕获 SIGSEGV/SIGABRT/SIGFPE/SIGILL/SIGBUS，
 *          记录信号号与 PC/SP 后 _exit。
 *
 * 说明：进程退出后记录随内存消失，这里主要用于本地打印定位；
 *       与 MCU 共用 iot_crash_record()/iot_crash_print() 的格式。
 */

#include "iot_crash.h"

#if defined(IOT_PLATFORM_PC) && IOT_CRASH_ENABLE

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* PC 进程退出后内存不保留，崩溃记录使用内核自带普通静态区 */
void *iot_port_crash_buffer(uint32_t size)
{
    (void)size;
    return NULL;
}

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static LONG WINAPI iot_pc_exception_filter(EXCEPTION_POINTERS *info)
{
    iot_crash_context_t ctx;
    DWORD code = 0U;

    memset(&ctx, 0, sizeof(ctx));
    if (info != NULL && info->ExceptionRecord != NULL) {
        code = info->ExceptionRecord->ExceptionCode;
        ctx.regs[6] = (uint32_t)(uintptr_t)
                      info->ExceptionRecord->ExceptionAddress;
    }
#if defined(_M_X64) || defined(__x86_64__)
    if (info != NULL && info->ContextRecord != NULL) {
        ctx.sp = (uint32_t)info->ContextRecord->Rsp;
    }
#else
    if (info != NULL && info->ContextRecord != NULL) {
        ctx.sp = (uint32_t)info->ContextRecord->Esp;
    }
#endif

    ctx.code = (uint32_t)code;
    ctx.expr = "unhandled exception";

    (void)iot_crash_record(
        (code == EXCEPTION_STACK_OVERFLOW) ? IOT_CRASH_STACK_OVERFLOW
                                           : IOT_CRASH_SIGNAL,
        &ctx);
    iot_crash_print(iot_crash_get());
    return EXCEPTION_EXECUTE_HANDLER;
}

void iot_crash_port_install(void)
{
    SetUnhandledExceptionFilter(iot_pc_exception_filter);
}

#else /* POSIX */

#include <signal.h>
#include <ucontext.h>
#include <unistd.h>

static void iot_pc_signal_handler(int sig, siginfo_t *info, void *context)
{
    iot_crash_context_t ctx;

    (void)info;
    memset(&ctx, 0, sizeof(ctx));

    ctx.code = (uint32_t)sig;
    ctx.expr = "fatal signal";

#if defined(__x86_64__)
    {
        ucontext_t *uc = (ucontext_t *)context;

        ctx.regs[6] = (uint32_t)uc->uc_mcontext.gregs[REG_RIP];
        ctx.sp = (uint32_t)uc->uc_mcontext.gregs[REG_RSP];
    }
#elif defined(__i386__)
    {
        ucontext_t *uc = (ucontext_t *)context;

        ctx.regs[6] = (uint32_t)uc->uc_mcontext.gregs[REG_EIP];
        ctx.sp = (uint32_t)uc->uc_mcontext.gregs[REG_ESP];
    }
#else
    (void)context;
#endif

    (void)iot_crash_record(IOT_CRASH_SIGNAL, &ctx);
    iot_crash_print(iot_crash_get());
    _exit(128 + sig);
}

void iot_crash_port_install(void)
{
    static const int signals[] = {
        SIGSEGV, SIGABRT, SIGFPE, SIGILL
#ifdef SIGBUS
        , SIGBUS
#endif
    };
    struct sigaction sa;
    size_t i;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = iot_pc_signal_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    for (i = 0U; i < sizeof(signals) / sizeof(signals[0]); i++) {
        (void)sigaction(signals[i], &sa, NULL);
    }
}

#endif /* _WIN32 */

#endif /* IOT_PLATFORM_PC && IOT_CRASH_ENABLE */
