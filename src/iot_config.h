#ifndef IOT_CONFIG_H
#define IOT_CONFIG_H

/*
 * iot_config.h - 内核编译期配置
 *
 * 所有裁剪开关集中在此，默认值面向“功能完整 + 可回归测试”；
 * 资源受限时逐项关闭即可，关闭的模块不会产生任何代码。
 */

/* ------------------------------ 断言 ------------------------------ */

/* Debug 构建启用断言；定义 NDEBUG 或显式置 0 可关闭 */
#ifndef IOT_ASSERT_ENABLE
#  ifdef NDEBUG
#    define IOT_ASSERT_ENABLE 0
#  else
#    define IOT_ASSERT_ENABLE 1
#  endif
#endif

/* ------------------------------ 调度 ------------------------------ */

/* 同优先级时间片轮转：每 N 个 tick 把当前任务放回同优先级队尾 */
#ifndef IOT_USE_TIME_SLICING
#define IOT_USE_TIME_SLICING 1
#endif

#ifndef IOT_TIME_SLICE_TICKS
#define IOT_TIME_SLICE_TICKS 1U
#endif

/* ------------------------------ 可靠性 ------------------------------ */

/* 栈溢出检测：创建时在栈底写入魔数，切换时校验 */
#ifndef IOT_USE_STACK_CHECK
#define IOT_USE_STACK_CHECK 1
#endif

#ifndef IOT_STACK_MAGIC
#define IOT_STACK_MAGIC 0xC0DEFACEUL
#endif

/* 互斥量优先级继承（含阻塞链递归传播） */
#ifndef IOT_USE_PRIORITY_INHERIT
#define IOT_USE_PRIORITY_INHERIT 1
#endif

/* 信号量计数上限，防止无界 give 溢出回绕 */
#ifndef IOT_SEM_MAX_COUNT
#define IOT_SEM_MAX_COUNT 0x7FFFFFFFUL
#endif

/* 队列深度是否为 2 的幂：打开后非 2 次幂深度直接拒绝，换取掩码取模 */
#ifndef IOT_QUEUE_REQUIRE_POW2
#define IOT_QUEUE_REQUIRE_POW2 0
#endif

/* ------------------------------ 功能模块 ------------------------------ */

/* 运行时统计：上下文切换次数、每任务累计运行 tick 与被调度次数 */
#ifndef IOT_USE_STATS
#define IOT_USE_STATS 1
#endif

/* 事件组（多事件同步） */
#ifndef IOT_USE_EVENT
#define IOT_USE_EVENT 1
#endif

/* 任务通知（轻量单值同步） */
#ifndef IOT_USE_NOTIFY
#define IOT_USE_NOTIFY 1
#endif

/* tickless 空闲：空闲任务按最近截止点休眠，避免固定 tick 唤醒 */
#ifndef IOT_USE_TICKLESS
#define IOT_USE_TICKLESS 1
#endif

#endif /* IOT_CONFIG_H */
