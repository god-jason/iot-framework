/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 robotos contributors
 *
 * ros_version.c - 版本 / 构建时间 / 编译配置 / 许可证描述
 *
 * 除 __DATE__ / __TIME__ 外全部由编译期宏决定，无运行期初始化。
 */

#include "ros_version.h"
#include "ros_config.h"
#include "ros_types.h"      /* ROS_MAX_PRIORITY */

#include <stdio.h>
#include <string.h>

/* ------------------------------ 平台 / 编译器 ------------------------------ */

#if defined(ROS_PLATFORM_STM32)
#  if defined(ROS_STM32_F1)
#    define ROS_VER_PLATFORM "STM32F1"
#  elif defined(ROS_STM32_F4)
#    define ROS_VER_PLATFORM "STM32F4"
#  elif defined(ROS_STM32_H7)
#    define ROS_VER_PLATFORM "STM32H7"
#  else
#    define ROS_VER_PLATFORM "STM32"
#  endif
#elif defined(ROS_PLATFORM_PC)
#  if defined(_WIN32)
#    define ROS_VER_PLATFORM "Windows"
#  elif defined(__linux__)
#    define ROS_VER_PLATFORM "Linux"
#  elif defined(__APPLE__)
#    define ROS_VER_PLATFORM "macOS"
#  else
#    define ROS_VER_PLATFORM "PC"
#  endif
#elif defined(ROS_PLATFORM_AVR)
#  define ROS_VER_PLATFORM "AVR"
#else
#  define ROS_VER_PLATFORM "unknown"
#endif

#if defined(__clang__)
#  define ROS_VER_COMPILER "Clang " __clang_version__
#elif defined(__GNUC__)
#  define ROS_VER_COMPILER "GCC " __VERSION__
#elif defined(_MSC_VER)
#  define ROS_VER_COMPILER "MSVC " ROS_VERSION_STR(_MSC_VER)
#else
#  define ROS_VER_COMPILER "unknown"
#endif

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#  define ROS_VER_C_STD "C11"
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
#  define ROS_VER_C_STD "C99"
#else
#  define ROS_VER_C_STD "C89"
#endif

/* ------------------------------ 特性位 ------------------------------ */

#define ROS_VERSION_FEATURES                                        \
    ((ROS_USE_TRACE            ? ROS_FEATURE_TRACE          : 0UL) | \
     (ROS_ASSERT_ENABLE        ? ROS_FEATURE_ASSERT         : 0UL) | \
     (ROS_CRASH_ENABLE         ? ROS_FEATURE_CRASH          : 0UL) | \
     (ROS_USE_STACK_CHECK      ? ROS_FEATURE_STACK_CHECK    : 0UL) | \
     (ROS_USE_PRIORITY_INHERIT ? ROS_FEATURE_PRIO_INHERIT   : 0UL) | \
     (ROS_USE_TIME_SLICING     ? ROS_FEATURE_TIME_SLICING   : 0UL) | \
     (ROS_USE_STATS            ? ROS_FEATURE_STATS          : 0UL) | \
     (ROS_USE_EVENT            ? ROS_FEATURE_EVENT          : 0UL) | \
     (ROS_USE_SBUF             ? ROS_FEATURE_SBUF           : 0UL) | \
     (ROS_USE_MSGQ             ? ROS_FEATURE_MSGQ           : 0UL) | \
     (ROS_USE_NOTIFY           ? ROS_FEATURE_NOTIFY         : 0UL) | \
     (ROS_USE_TICKLESS         ? ROS_FEATURE_TICKLESS       : 0UL) | \
     (ROS_USE_TIMER_TASK       ? ROS_FEATURE_TIMER_TASK     : 0UL))

/* ------------------------------ 描述符 ------------------------------ */

static const ros_version_info_t s_version = {
    ROS_VERSION_MAJOR,
    ROS_VERSION_MINOR,
    ROS_VERSION_PATCH,
    ROS_VERSION_SEMVER,
    ROS_VERSION_PRERELEASE,
    ROS_VERSION_BUILD,
    ROS_PROJECT_NAME,
    ROS_LICENSE_NAME,
    ROS_LICENSE_SPDX,
    ROS_LICENSE_COPYRIGHT,
    __DATE__ " " __TIME__,
    ROS_VER_PLATFORM,
    ROS_VER_COMPILER,
    ROS_VER_C_STD,
    (uint32_t)ROS_TICK_HZ,
    (uint16_t)ROS_MAX_PRIORITY,
    (uint32_t)ROS_VERSION_FEATURES
};

/* ------------------------------ 接口 ------------------------------ */

const ros_version_info_t *ros_version_get(void)
{
    return &s_version;
}

const char *ros_version_string(void)
{
    return ROS_VERSION_SEMVER;
}

const char *ros_build_datetime(void)
{
    return __DATE__ " " __TIME__;
}

bool ros_version_has_feature(uint32_t feature)
{
    return (feature != 0U) && ((s_version.features & feature) == feature);
}

/* 把特性位图渲染成空格分隔的名字列表 */
static void ros_version_features_text(uint32_t features, char *buf, size_t cap)
{
    static const struct {
        uint32_t    bit;
        const char *name;
    } table[] = {
        { ROS_FEATURE_TRACE,        "trace" },
        { ROS_FEATURE_ASSERT,       "assert" },
        { ROS_FEATURE_CRASH,        "crash" },
        { ROS_FEATURE_STACK_CHECK,  "stack_check" },
        { ROS_FEATURE_PRIO_INHERIT, "prio_inherit" },
        { ROS_FEATURE_TIME_SLICING, "time_slice" },
        { ROS_FEATURE_STATS,        "stats" },
        { ROS_FEATURE_EVENT,        "event" },
        { ROS_FEATURE_SBUF,         "sbuf" },
        { ROS_FEATURE_MSGQ,         "msgq" },
        { ROS_FEATURE_NOTIFY,       "notify" },
        { ROS_FEATURE_TICKLESS,     "tickless" },
        { ROS_FEATURE_TIMER_TASK,   "timer_task" }
    };
    size_t pos = 0U;
    bool any = false;
    uint32_t i;

    if (buf == NULL || cap == 0U) {
        return;
    }
    buf[0] = '\0';

    for (i = 0U; i < (uint32_t)(sizeof(table) / sizeof(table[0])); i++) {
        int n;

        if ((features & table[i].bit) == 0U) {
            continue;
        }
        n = snprintf(&buf[pos], cap - pos, "%s%s",
                     any ? " " : "", table[i].name);
        if (n < 0) {
            break;
        }
        any = true;
        if ((size_t)n >= cap - pos) {
            pos = cap - 1U;         /* 已截断 */
            break;
        }
        pos += (size_t)n;
    }
    if (!any) {
        (void)snprintf(buf, cap, "none");
    }
}

int ros_version_format(char *buf, size_t cap)
{
    char features[192];
    const ros_version_info_t *v = &s_version;
    const char *pre;
    const char *bld;
    int n;

    if (buf == NULL || cap == 0U) {
        return -1;
    }

    ros_version_features_text(v->features, features, sizeof(features));
    pre = (v->prerelease[0] != '\0') ? "-" : "";
    bld = (v->build[0] != '\0') ? "+" : "";

    n = snprintf(buf, cap,
                 "%s %s%s%s%s%s\n"
                 "  compiled : %s\n"
                 "  platform : %s\n"
                 "  compiler : %s (%s)\n"
                 "  kernel   : tick=%uHz max_priority=%u\n"
                 "  license  : %s (SPDX: %s)\n"
                 "  %s\n"
                 "  features : %s\n",
                 v->project, v->semver,
                 pre, v->prerelease, bld, v->build,
                 v->datetime,
                 v->platform,
                 v->compiler, v->c_standard,
                 (unsigned)v->tick_hz, (unsigned)v->max_priority,
                 v->license, v->license_spdx,
                 v->copyright,
                 features);
    if (n < 0) {
        return -1;
    }
    return (n < (int)cap) ? n : (int)cap - 1;
}
