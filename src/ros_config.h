#ifndef ROS_CONFIG_H
#define ROS_CONFIG_H

/*
 * ros_config.h - 内核编译期配置
 *
 * 所有裁剪开关集中在此，默认值面向“功能完整 + 可回归测试”；
 * 资源受限时逐项关闭即可，关闭的模块不会产生任何代码。
 */

/* ------------------------------ 调度 ------------------------------ */

/*
 * tick 频率（Hz）。内核把所有 *_ms 参数与定时器周期都直接当作 tick 计数，
 * 因此该值必须为 1000；改用其它时基时需同步引入 ms→tick 换算。
 */
#ifndef ROS_TICK_HZ
#define ROS_TICK_HZ 1000U
#endif

/* 优先级数量：0..ROS_MAX_PRIORITY-1（数值越小优先级越高） */
#ifndef ROS_MAX_PRIORITY
#define ROS_MAX_PRIORITY 8
#endif

/* 最高优先级（最紧急） */
#define ROS_PRIORITY_HIGHEST 0U

/* 最低优先级，保留给内置空闲任务 */
#define ROS_PRIORITY_IDLE (ROS_MAX_PRIORITY - 1)

/* 同优先级时间片轮转：每 N 个 tick 把当前任务放回同优先级队尾 */
#ifndef ROS_USE_TIME_SLICING
#define ROS_USE_TIME_SLICING 1
#endif

#ifndef ROS_TIME_SLICE_TICKS
#define ROS_TIME_SLICE_TICKS 1U
#endif

/* ------------------------------ 可靠性 ------------------------------ */

/* 栈溢出检测：创建时在栈底写入魔数，切换时校验 */
#ifndef ROS_USE_STACK_CHECK
#define ROS_USE_STACK_CHECK 1
#endif

#ifndef ROS_STACK_MAGIC
#define ROS_STACK_MAGIC 0xC0DEFACEUL
#endif

/* 互斥量优先级继承（含阻塞链递归传播） */
#ifndef ROS_USE_PRIORITY_INHERIT
#define ROS_USE_PRIORITY_INHERIT 0
#endif

/* 信号量计数上限，防止无界 give 溢出回绕 */
#ifndef ROS_SEM_MAX_COUNT
#define ROS_SEM_MAX_COUNT 0x7FFFFFFFUL
#endif

/* 队列深度是否为 2 的幂：打开后非 2 次幂深度直接拒绝，换取掩码取模 */
#ifndef ROS_QUEUE_REQUIRE_POW2
#define ROS_QUEUE_REQUIRE_POW2 0
#endif

/* ------------------------------ 功能模块 ------------------------------ */

/* 运行时统计：上下文切换次数、每任务累计运行 tick 与被调度次数 */
#ifndef ROS_USE_STATS
#define ROS_USE_STATS 1
#endif

/*
 * 堆内存归属统计：按任务记账 ros_mem_* 的分配/释放（ps 的 MEM 列、state 汇总）。
 * 打开后每次分配多一个指针对齐的头部（ARM32 8 字节，x86-64 16 字节）。
 */
#ifndef ROS_USE_MEM_STATS
#define ROS_USE_MEM_STATS 1
#endif

/* 事件组（多事件同步） */
#ifndef ROS_USE_EVENT
#define ROS_USE_EVENT 1
#endif

/* 流式缓冲：字节流环形缓冲，支持阻塞读/写与 FromISR 非阻塞版本 */
#ifndef ROS_USE_SBUF
#define ROS_USE_SBUF 1
#endif

/* 变长消息队列：带长度头的环形消息缓冲（区分 ros_queue 的定长消息） */
#ifndef ROS_USE_MSGQ
#define ROS_USE_MSGQ 1
#endif

/* 软件定时器回调是否由专用内核任务执行（1 = 任务上下文，0 = tick 上下文） */
#ifndef ROS_USE_TIMER_TASK
#define ROS_USE_TIMER_TASK 1
#endif

/* 定时器任务优先级（0 最高；须小于 ROS_PRIORITY_IDLE） */
#ifndef ROS_TIMER_TASK_PRIORITY
#define ROS_TIMER_TASK_PRIORITY 2U
#endif

/* 定时器任务栈大小（字节），回调应短小，避免大局部变量 */
#ifndef ROS_TIMER_TASK_STACK_SIZE
#define ROS_TIMER_TASK_STACK_SIZE 1024U
#endif

/* 任务通知（轻量单值同步） */
#ifndef ROS_USE_NOTIFY
#define ROS_USE_NOTIFY 1
#endif

/* tickless 空闲：空闲任务按最近截止点休眠，避免固定 tick 唤醒 */
#ifndef ROS_USE_TICKLESS
#define ROS_USE_TICKLESS 1
#endif

/* ------------------------------ 事件跟踪 ------------------------------ */

/*
 * 事件跟踪：编译期总开关。打开后宏仍受运行期 g_ros_trace_level 控制，
 * 默认 ROS_TRACE_DEFAULT_LEVEL = OFF，因此编译进去也不产生事件。
 * 置 0 则所有 ROS_TRACE_* 宏编译为空，零 RAM/ROM 开销。
 */
#ifndef ROS_USE_TRACE
#define ROS_USE_TRACE 0
#endif

/* ------------------------------ 配置一致性 ------------------------------ */
/*
 * 这些不变式原先只写在注释里，裁剪配置时一旦违反会在运行期静默失败，
 * 这里改为编译期直接报错。
 */
#if ROS_TICK_HZ != 1000U
#error "内核所有 *_ms 参数直接按 tick 计数，ROS_TICK_HZ 必须为 1000"
#endif

#if ROS_MAX_PRIORITY < 2
#error "ROS_MAX_PRIORITY 至少为 2（需要独立的最低优先级给空闲任务）"
#endif

#if ROS_USE_TIMER_TASK && (ROS_TIMER_TASK_PRIORITY >= (ROS_MAX_PRIORITY - 1))
#error "ROS_TIMER_TASK_PRIORITY 必须小于空闲任务优先级（ROS_MAX_PRIORITY-1）"
#endif

#if ROS_USE_TIME_SLICING && (ROS_TIME_SLICE_TICKS == 0U)
#error "ROS_TIME_SLICE_TICKS 必须大于 0"
#endif

#endif /* ROS_CONFIG_H */
