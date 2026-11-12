#ifndef ROS_ASSERT_H
#define ROS_ASSERT_H

/*
 * ros_assert.h - 内核断言
 *
 * 断言用于捕获“不该发生”的内部状态（链表不一致、非法状态迁移等）。
 * 失败时打印文件/行号/表达式，然后停机（PC 端退出进程，MCU 端死循环）。
 * 移植层可提供弱符号 ros_assert_fail() 覆写默认行为。
 */

#include "ros_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if ROS_ASSERT_ENABLE

void ros_assert_fail(const char *file, int line, const char *expr);

#define ROS_ASSERT(cond)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ros_assert_fail(__FILE__, __LINE__, #cond);                      \
        }                                                                    \
    } while (0)

#else /* ROS_ASSERT_ENABLE */

#define ROS_ASSERT(cond) ((void)0)

#endif /* ROS_ASSERT_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* ROS_ASSERT_H */
