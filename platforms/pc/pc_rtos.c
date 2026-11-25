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
 * ros_port_idle_timeout() -> ros_tick_increment() 批量推进到下一个截止点。
 *
 * 注意：本文件只在主机模拟目标中编译，MCU 端使用 platforms/stm32/stm32_rtos.c。
 */

#if defined(ROS_PLATFORM_PC)

#include "ros.h"
#include "common.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * 默认按宿主系统选择；定义 ROS_PC_FORCE_POSIX 可在 Windows 上强制编译
 * ucontext 分支（用于交叉语法检查），正常构建无需定义。
 */
#if defined(_WIN32) && !defined(ROS_PC_FORCE_POSIX)
#define ROS_PC_WIN32_FIBER 1
#endif

#if defined(ROS_PC_WIN32_FIBER)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static LPVOID s_main_fiber;

/* Fiber 起始例程：运行任务入口，返回后退出该任务 */
static void WINAPI fiber_proc(void *param)
{
    ros_task_t *task = (ros_task_t *)param;

    if (task->entry != NULL) {
        task->entry(task->arg);
    }
    ros_task_exit();
}

#else /* POSIX (Linux / macOS) */

#include <ucontext.h>

/* 64 位平台把指针拆成两个 int 传给 makecontext */
#if (defined(__SIZEOF_POINTER__) && (__SIZEOF_POINTER__ == 8)) || \
    (defined(UINTPTR_MAX) && (UINTPTR_MAX > 0xFFFFFFFFUL))
#define ROS_PC_PTR64 1
#endif

#define ROS_PC_MAX_CONTEXTS  64
#define ROS_PC_STACK_SIZE    (64U * 1024U)

typedef struct {
    ucontext_t ctx;
    uint8_t    stack[ROS_PC_STACK_SIZE];
    bool       used;
} pc_context_t;

static pc_context_t s_contexts[ROS_PC_MAX_CONTEXTS];
static ucontext_t   s_main_context;

/* ucontext 入口：makecontext 只接受 int 参数，需拆分/还原指针 */
static void fiber_proc(int lo, int hi)
{
    uintptr_t p = (uintptr_t)(uint32_t)lo;
    ros_task_t *task;

#ifdef ROS_PC_PTR64
    p |= ((uintptr_t)(uint32_t)hi << 32);
#else
    (void)hi;
#endif

    task = (ros_task_t *)p;
    if (task->entry != NULL) {
        task->entry(task->arg);
    }
    ros_task_exit();
}

#endif /* ROS_PC_WIN32_FIBER */

void ros_port_init(void)
{
#if defined(ROS_PC_WIN32_FIBER)
    if (s_main_fiber == NULL) {
        s_main_fiber = ConvertThreadToFiber(NULL);
    }
#else
    memset(s_contexts, 0, sizeof(s_contexts));
#endif
}

int ros_port_stack_init(ros_task_t *task)
{
    if (task == NULL) {
        return ROS_INVALID_PARAM;
    }

#if defined(ROS_PC_WIN32_FIBER)
    {
        void *fiber = CreateFiber((SIZE_T)task->stack_size,
                                  fiber_proc, (LPVOID)task);

        if (fiber == NULL) {
            LOG_E(ROS_TAG, "CreateFiber failed, stack_size=%u",
                      (unsigned)task->stack_size);
            return ROS_NO_MEMORY;
        }
        task->stack_ptr = fiber;
    }
#else
    {
        pc_context_t *slot = NULL;
        uintptr_t ptr;
        int i;

        for (i = 0; i < ROS_PC_MAX_CONTEXTS; i++) {
            if (!s_contexts[i].used) {
                slot = &s_contexts[i];
                break;
            }
        }
        if (slot == NULL) {
            LOG_E(ROS_TAG, "ucontext pool exhausted (max %d tasks)",
                      ROS_PC_MAX_CONTEXTS);
            return ROS_NO_MEMORY;
        }

        if (getcontext(&slot->ctx) != 0) {
            LOG_E(ROS_TAG, "getcontext failed");
            return ROS_NO_MEMORY;
        }

        /* 静态栈保证 16 字节对齐；不占用调用者的 stack_mem，
         * 该内存仍用于魔数填充与栈水位统计（与 Windows Fiber 一致）。 */
        ptr = (uintptr_t)slot->stack;
        slot->ctx.uc_stack.ss_sp = (void *)((ptr + 15U) & ~(uintptr_t)15U);
        slot->ctx.uc_stack.ss_size = ROS_PC_STACK_SIZE - 16U;
        slot->ctx.uc_stack.ss_flags = 0;
        slot->ctx.uc_link = &s_main_context;

#ifdef ROS_PC_PTR64
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

    return ROS_OK;
}

void ros_port_stack_deinit(ros_task_t *task)
{
    if (task == NULL || task->stack_ptr == NULL) {
        return;
    }

#if defined(ROS_PC_WIN32_FIBER)
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

void ros_port_start_first(ros_task_t *task)
{
#if defined(ROS_PC_WIN32_FIBER)
    SwitchToFiber(task->stack_ptr);
#else
    swapcontext(&s_main_context, &((pc_context_t *)task->stack_ptr)->ctx);
#endif
}

void ros_port_switch(ros_task_t *from, ros_task_t *to)
{
    (void)from;

#if defined(ROS_PC_WIN32_FIBER)
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

void ros_port_return(void)
{
#if defined(ROS_PC_WIN32_FIBER)
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
 *
 * 定义 ROS_PC_REAL_TIME 时（交互演示目标）额外按墙钟节奏真实等待，
 * 使 1 个 tick ≈ 1ms 真实时间：任务里 ros_sleep_ms(1000) 就是现实 1 秒。
 * 单元测试不加该宏，保持"时间瞬间推进"以便快速回归。
 */
#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>       /* timeBeginPeriod / timeEndPeriod */
#else
#include <time.h>
#endif

/* 单调墙钟（微秒），用于把模拟 tick 对齐到真实时间 */
static uint64_t pc_now_us(void)
{
#if defined(_WIN32)
    static LARGE_INTEGER freq;
    LARGE_INTEGER c;

    if (freq.QuadPart == 0) {
        (void)QueryPerformanceFrequency(&freq);
    }
    (void)QueryPerformanceCounter(&c);
    return (uint64_t)((c.QuadPart * 1000000LL) / freq.QuadPart);
#else
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000L);
#endif
}

static void pc_sleep_ms(uint32_t ms)
{
#if defined(_WIN32)
    Sleep(ms);
#else
    struct timespec ts;

    ts.tv_sec = (time_t)(ms / 1000U);
    ts.tv_nsec = (long)(ms % 1000U) * 1000000L;
    (void)nanosleep(&ts, NULL);
#endif
}

static bool s_real_time;
static uint64_t s_wall_base_us;

/*
 * 运行时开关：开启后模拟 tick 跟随墙钟（1 tick ≈ 1ms 真实时间），
 * 默认关闭以保持单元测试“瞬间推进”。必须在调度器运行后调用。
 */
void ros_pc_set_real_time(bool on)
{
    if (on == s_real_time) {
        return;
    }
    s_real_time = on;
#if defined(_WIN32)
    if (on) {
        (void)timeBeginPeriod(1);       /* 提升 Sleep 粒度到 ~1ms */
    } else {
        (void)timeEndPeriod(1);
    }
#endif
    if (on) {
        uint64_t now = pc_now_us();
        uint64_t t = (uint64_t)ros_tick_count * 1000ULL;

        s_wall_base_us = (now > t) ? (now - t) : 0U;
    }
}

bool ros_pc_real_time(void)
{
    return s_real_time;
}

/*
 * 把当前 tick 对齐到墙钟：粗睡到目标前 2ms，剩余自旋。
 * 这样既有 Sleep 的低占用，又能获得亚毫秒精度且不累积漂移。
 */
static void pc_wait_until_tick(uint32_t tick)
{
    uint64_t target;
    uint64_t now;

    if (!s_real_time) {
        return;
    }
    target = s_wall_base_us + (uint64_t)tick * 1000ULL;
    for (;;) {
        now = pc_now_us();
        if (now >= target) {
            break;
        }
        if ((target - now) > 2000ULL) {
            uint32_t ms = (uint32_t)((target - now - 1000ULL) / 1000ULL);

            if (ms > 0U) {
                pc_sleep_ms(ms);
            }
        }
    }
}

void ros_port_idle(void)
{
    uint32_t now = ros_tick_count;
    uint32_t delayed = ros_delayed_next_deadline();
    uint32_t timer = ros_timer_next_deadline();
    uint32_t next = (delayed < timer) ? delayed : timer;
    uint32_t delta;

    if (next == UINT32_MAX) {
        delta = 1U;
    } else if (ROS_TICK_AFTER(next, now)) {
        delta = next - now;
    } else {
        delta = 1U;
    }
    pc_wait_until_tick(now + delta);
    ros_tick_increment(delta);
}

/*
 * tickless 空闲：直接把模拟时间推进到最近的截止点。
 * ms 为 UINT32_MAX 表示没有截止点，推进 1 tick 避免空转停滞。
 */
void ros_port_idle_timeout(uint32_t ms)
{
    uint32_t delta = (ms == 0U || ms == UINT32_MAX) ? 1U : ms;

    pc_wait_until_tick(ros_tick_count + delta);
    ros_tick_increment(delta);
}

void ros_port_request_switch(void)
{
    /* 协作式无抢占：时间片轮转在下一次 ros_yield() 生效 */
}

bool ros_port_in_isr(void)
{
    /* 主机模拟端全部逻辑都跑在任务上下文（Fiber / ucontext） */
    return false;
}

void ros_port_critical_enter(void)
{
    /* 单线程协作式，无抢占，临界区为空 */
}

void ros_port_critical_exit(void)
{
    /* 单线程协作式，无抢占，临界区为空 */
}

void ros_port_fatal_halt(void)
{
    /* 模拟端无法停住 CPU：退出进程，便于 CI 判定失败 */
    exit(1);
}

#endif /* ROS_PLATFORM_PC */
