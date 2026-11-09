/*
 * iot_assert.c - 断言失败处理
 *
 * 行为：
 *   - 先把断言位置写入崩溃记录（.noinit，复位后仍可读取）；
 *   - 打印完整现场（原因/任务/寄存器/固件版本）；
 *   - PC 模拟端退出进程（便于 CI 直接判定失败）；
 *   - MCU 端关中断后死循环，保留现场给调试器。
 */

#include "iot_config.h"

#if IOT_ASSERT_ENABLE

#include <stdlib.h>
#include <string.h>

#include "iot_types.h"
#include "iot_port.h"
#include "iot_crash.h"

/*
 * 断言失败入口（由 IOT_ASSERT 宏调用）。
 * 参数：file/line/expr 为断言位置，file/expr 可能为 NULL。
 * 处理：写崩溃记录 -> 打印现场 -> 关中断 -> PC 退出 / MCU 死循环。
 */
void iot_assert_fail(const char *file, int line, const char *expr)
{
    iot_crash_context_t ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.file = file;
    ctx.line = (uint32_t)line;
    ctx.expr = expr;

    /* 同一次启动内只保留第一次现场，后续断言不会覆盖根因 */
    (void)iot_crash_record(IOT_CRASH_ASSERT, &ctx);
    iot_crash_print(iot_crash_get());

    iot_port_critical_enter();

#if defined(IOT_PLATFORM_PC)
    /* 模拟端无法停住 CPU，直接退出让 CI 失败 */
    exit(1);
#else
    for (;;) {
        /* 保持中断关闭，便于调试器连接后查看调用栈 */
    }
#endif
}

#endif /* IOT_ASSERT_ENABLE */
