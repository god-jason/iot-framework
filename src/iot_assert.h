#ifndef IOT_ASSERT_H
#define IOT_ASSERT_H

/*
 * iot_assert.h - 内核断言
 *
 * 断言用于捕获“不该发生”的内部状态（链表不一致、非法状态迁移等）。
 * 失败时打印文件/行号/表达式，然后停机（PC 端退出进程，MCU 端死循环）。
 * 移植层可提供弱符号 iot_assert_fail() 覆写默认行为。
 */

#include "iot_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if IOT_ASSERT_ENABLE

void iot_assert_fail(const char *file, int line, const char *expr);

#define IOT_ASSERT(cond)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            iot_assert_fail(__FILE__, __LINE__, #cond);                      \
        }                                                                    \
    } while (0)

#else /* IOT_ASSERT_ENABLE */

#define IOT_ASSERT(cond) ((void)0)

#endif /* IOT_ASSERT_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* IOT_ASSERT_H */
