/*
 * ros_assert.c - 断言失败处理
 *
 * 行为：
 *   - 先把断言位置写入崩溃记录（.noinit，复位后仍可读取）；
 *   - 打印完整现场（原因/任务/寄存器/固件版本）；
 *   - PC 模拟端退出进程（便于 CI 直接判定失败）；
 *   - MCU 端关中断后死循环，保留现场给调试器。
 */

#include "ros_config.h"

#if ROS_ASSERT_ENABLE

#include <string.h>

#include "ros_types.h"
#include "ros_port.h"
#include "ros_crash.h"
#include "ros_trace.h"

/*
 * 断言失败入口（由 ROS_ASSERT 宏调用）。
 * 参数：file/line/expr 为断言位置，file/expr 可能为 NULL。
 * 处理：写崩溃记录 -> 打印现场 -> 关中断 -> PC 退出 / MCU 死循环。
 */
void ros_assert_fail(const char *file, int line, const char *expr)
{
    ros_crash_context_t ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.file = file;
    ctx.line = (uint32_t)line;
    ctx.expr = expr;

    /* 崩溃记录会冻结 trace，断言事件要先于写现场发出 */
    ROS_TRACE(ROS_EVT_ASSERT, (uint32_t)line, (uint32_t)(uintptr_t)expr);

    /* 同一次启动内只保留第一次现场，后续断言不会覆盖根因 */
    (void)ros_crash_record(ROS_CRASH_ASSERT, &ctx);
    ros_crash_print(ros_crash_get());

    ros_port_critical_enter();

    /* 平台相关停机方式：模拟端退出进程，MCU 端死循环等待调试器 */
    ros_port_fatal_halt();
}

#endif /* ROS_ASSERT_ENABLE */
