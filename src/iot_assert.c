/*
 * iot_assert.c - 断言失败处理
 *
 * 默认行为：
 *   - 打印断言位置；
 *   - PC 模拟端退出进程（便于 CI 直接判定失败）；
 *   - MCU 端关中断后死循环，保留现场给调试器。
 */

#include "iot_config.h"

#if IOT_ASSERT_ENABLE

#include <stdlib.h>

#include "iot_types.h"
#include "iot_port.h"
#include "log.h"

void iot_assert_fail(const char *file, int line, const char *expr)
{
    LOG_E(IOT_TAG, "ASSERT FAILED: %s:%d: %s",
              (file != NULL) ? file : "?", line, (expr != NULL) ? expr : "?");

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
