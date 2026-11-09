/*
 * pc_rtos.c - RTOS 主机模拟移植层（协作式，独立栈）
 *
 * 两套等价实现，由编译平台自动选择：
 *   - Windows : Win32 Fiber（ConvertThreadToFiber / CreateFiber / SwitchToFiber）；
 *   - Linux   : POSIX ucontext（getcontext / makecontext / swapcontext），
 *               每个任务一块静态栈，行为与 Fiber 一致。
 *
 * 两者都由宿主内核提供真实独立栈与上下文切换，因此 RTOS 内核源码无需区分。
 * 协作式模型下没有硬件抢占，时间推进由空闲任务调用
 * iot_port_idle_timeout() -> iot_tick_increment() 批量推进到下一个截止点。
 *
 * 注意：本文件只在主机模拟目标中编译，MCU 端使用 platforms/stm32/stm32_rtos.c。
 */

#if defined(IOT_PLATFORM_PC)

#include "iot.h"
#include "iot_port.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * 默认按宿主系统选择；定义 IOT_PC_FORCE_POSIX 可在 Windows 上强制编译
 * ucontext 分支（用于交叉语法检查），正常构建无需定义。
 */
#if defined(_WIN32) && !defined(IOT_PC_FORCE_POSIX)
#define IOT_PC_WIN32_FIBER 1
#endif

#if defined(IOT_PC_WIN32_FIBER)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static LPVOID s_main_fiber;

/* Fiber 起始例程：运行任务入口，返回后退出该任务 */
static void WINAPI fiber_proc(void *param)
{
    iot_task_t *task = (iot_task_t *)param;

    if (task->entry != NULL) {
        task->entry(task->arg);
    }
    iot_task_exit();
}

#else /* POSIX (Linux / macOS) */

#include <ucontext.h>

/* 64 位平台把指针拆成两个 int 传给 makecontext */
#if (defined(__SIZEOF_POINTER__) && (__SIZEOF_POINTER__ == 8)) || \
    (defined(UINTPTR_MAX) && (UINTPTR_MAX > 0xFFFFFFFFUL))
#define IOT_PC_PTR64 1
#endif

#define IOT_PC_MAX_CONTEXTS  64
#define IOT_PC_STACK_SIZE    (64U * 1024U)

typedef struct {
    ucontext_t ctx;
    uint8_t    stack[IOT_PC_STACK_SIZE];
    bool       used;
} pc_context_t;

static pc_context_t s_contexts[IOT_PC_MAX_CONTEXTS];
static ucontext_t   s_main_context;

/* ucontext 入口：makecontext 只接受 int 参数，需拆分/还原指针 */
static void fiber_proc(int lo, int hi)
{
    uintptr_t p = (uintptr_t)(uint32_t)lo;
    iot_task_t *task;

#ifdef IOT_PC_PTR64
    p |= ((uintptr_t)(uint32_t)hi << 32);
#else
    (void)hi;
#endif

    task = (iot_task_t *)p;
    if (task->entry != NULL) {
        task->entry(task->arg);
    }
    iot_task_exit();
}

#endif /* IOT_PC_WIN32_FIBER */

void iot_port_init(void)
{
#if defined(IOT_PC_WIN32_FIBER)
    if (s_main_fiber == NULL) {
        s_main_fiber = ConvertThreadToFiber(NULL);
    }
#else
    memset(s_contexts, 0, sizeof(s_contexts));
#endif
}

int iot_port_stack_init(iot_task_t *task)
{
    if (task == NULL) {
        return IOT_INVALID_PARAM;
    }

#if defined(IOT_PC_WIN32_FIBER)
    {
        void *fiber = CreateFiber((SIZE_T)task->stack_size,
                                  fiber_proc, (LPVOID)task);

        if (fiber == NULL) {
            LOG_E(IOT_TAG, "CreateFiber failed, stack_size=%u",
                      (unsigned)task->stack_size);
            return IOT_NO_MEMORY;
        }
        task->stack_ptr = fiber;
    }
#else
    {
        pc_context_t *slot = NULL;
        uintptr_t ptr;
        int i;

        for (i = 0; i < IOT_PC_MAX_CONTEXTS; i++) {
            if (!s_contexts[i].used) {
                slot = &s_contexts[i];
                break;
            }
        }
        if (slot == NULL) {
            LOG_E(IOT_TAG, "ucontext pool exhausted (max %d tasks)",
                      IOT_PC_MAX_CONTEXTS);
            return IOT_NO_MEMORY;
        }

        if (getcontext(&slot->ctx) != 0) {
            LOG_E(IOT_TAG, "getcontext failed");
            return IOT_NO_MEMORY;
        }

        /* 静态栈保证 16 字节对齐；不占用调用者的 stack_mem，
         * 该内存仍用于魔数填充与栈水位统计（与 Windows Fiber 一致）。 */
        ptr = (uintptr_t)slot->stack;
        slot->ctx.uc_stack.ss_sp = (void *)((ptr + 15U) & ~(uintptr_t)15U);
        slot->ctx.uc_stack.ss_size = IOT_PC_STACK_SIZE - 16U;
        slot->ctx.uc_stack.ss_flags = 0;
        slot->ctx.uc_link = &s_main_context;

#ifdef IOT_PC_PTR64
        {
            uintptr_t p = (uintptr_t)task;

            makecontext(&slot->ctx, (void (*)(void))fiber_proc, 2,
                        (int)(uint32_t)(p & 0xFFFFFFFFUL),
                        (int)(uint32_t)(p >> 32));
        }
#else
        makecontext(&slot->ctx, (void (*)(void))fiber_proc, 1,
                    (int)(uintptr_t)task);
#endif

        slot->used = true;
        task->stack_ptr = slot;
    }
#endif

    return IOT_OK;
}

void iot_port_stack_deinit(iot_task_t *task)
{
    if (task == NULL || task->stack_ptr == NULL) {
        return;
    }

#if defined(IOT_PC_WIN32_FIBER)
    /* 主线程转换出的 Fiber 不能删除 */
    if (task->stack_ptr != s_main_fiber) {
        DeleteFiber(task->stack_ptr);
    }
#else
    /* 归还静态 ucontext 槽位，供后续任务复用 */
    ((pc_context_t *)task->stack_ptr)->used = false;
#endif
    task->stack_ptr = NULL;
}

void iot_port_start_first(iot_task_t *task)
{
#if defined(IOT_PC_WIN32_FIBER)
    SwitchToFiber(task->stack_ptr);
#else
    swapcontext(&s_main_context, &((pc_context_t *)task->stack_ptr)->ctx);
#endif
}

void iot_port_switch(iot_task_t *from, iot_task_t *to)
{
    (void)from;

#if defined(IOT_PC_WIN32_FIBER)
    SwitchToFiber(to->stack_ptr);
#else
    {
        pc_context_t *from_ctx = (from != NULL)
                               ? (pc_context_t *)from->stack_ptr : NULL;
        pc_context_t *to_ctx = (pc_context_t *)to->stack_ptr;

        if (from_ctx == NULL || to_ctx == NULL) {
            return;
        }
        swapcontext(&from_ctx->ctx, &to_ctx->ctx);
    }
#endif
}

void iot_port_return(void)
{
#if defined(IOT_PC_WIN32_FIBER)
    if (s_main_fiber != NULL) {
        SwitchToFiber(s_main_fiber);
    }
#else
    setcontext(&s_main_context);
#endif
}

/*
 * 主机端空闲处理：把模拟时间推进到最近的唤醒/定时截止点。
 *   - 没有任何时间点时每次推进 1 tick，避免空转过快；
 *   - 存在未来时间点时直接跳到该时刻；
 *   - 已到期时推进 1 tick 触发处理。
 */
void iot_port_idle(void)
{
    uint32_t now = iot_tick_count;
    uint32_t delayed = iot_delayed_next_deadline();
    uint32_t timer = iot_timer_next_deadline();
    uint32_t next = (delayed < timer) ? delayed : timer;

    if (next == UINT32_MAX) {
        iot_tick_increment(1U);
    } else if (IOT_TICK_AFTER(next, now)) {
        iot_tick_increment(next - now);
    } else {
        iot_tick_increment(1U);
    }
}

/*
 * tickless 空闲：直接把模拟时间推进到最近的截止点。
 * ms 为 UINT32_MAX 表示没有截止点，推进 1 tick 避免空转停滞。
 */
void iot_port_idle_timeout(uint32_t ms)
{
    if (ms == 0U || ms == UINT32_MAX) {
        iot_tick_increment(1U);
    } else {
        iot_tick_increment(ms);
    }
}

void iot_port_request_switch(void)
{
    /* 协作式无抢占：时间片轮转在下一次 iot_yield() 生效 */
}

bool iot_port_in_isr(void)
{
    /* 主机模拟端全部逻辑都跑在任务上下文（Fiber / ucontext） */
    return false;
}

void iot_port_critical_enter(void)
{
    /* 单线程协作式，无抢占，临界区为空 */
}

void iot_port_critical_exit(void)
{
    /* 单线程协作式，无抢占，临界区为空 */
}

void iot_port_fatal_halt(void)
{
    /* 模拟端无法停住 CPU：退出进程，便于 CI 判定失败 */
    exit(1);
}

#endif /* IOT_PLATFORM_PC */
