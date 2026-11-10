/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 iot-os contributors
 *
 * iot_version.c - 版本 / 构建时间 / 编译配置 / 许可证描述
 *
 * 除 __DATE__ / __TIME__ 外全部由编译期宏决定，无运行期初始化。
 */

#include "iot_version.h"
#include "iot_config.h"
#include "iot_types.h"      /* IOT_MAX_PRIORITY */

#include <stdio.h>
#include <string.h>

/* ------------------------------ 平台 / 编译器 ------------------------------ */

#if defined(IOT_PLATFORM_STM32)
#  if defined(IOT_STM32_F1)
#    define IOT_VER_PLATFORM "STM32F1"
#  elif defined(IOT_STM32_F4)
#    define IOT_VER_PLATFORM "STM32F4"
#  elif defined(IOT_STM32_H7)
#    define IOT_VER_PLATFORM "STM32H7"
#  else
#    define IOT_VER_PLATFORM "STM32"
#  endif
#elif defined(IOT_PLATFORM_PC)
#  if defined(_WIN32)
#    define IOT_VER_PLATFORM "Windows"
#  elif defined(__linux__)
#    define IOT_VER_PLATFORM "Linux"
#  elif defined(__APPLE__)
#    define IOT_VER_PLATFORM "macOS"
#  else
#    define IOT_VER_PLATFORM "PC"
#  endif
#elif defined(IOT_PLATFORM_AVR)
#  define IOT_VER_PLATFORM "AVR"
#else
#  define IOT_VER_PLATFORM "unknown"
#endif

#if defined(__clang__)
#  define IOT_VER_COMPILER "Clang " __clang_version__
#elif defined(__GNUC__)
#  define IOT_VER_COMPILER "GCC " __VERSION__
#elif defined(_MSC_VER)
#  define IOT_VER_COMPILER "MSVC " IOT_VERSION_STR(_MSC_VER)
#else
#  define IOT_VER_COMPILER "unknown"
#endif

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#  define IOT_VER_C_STD "C11"
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
#  define IOT_VER_C_STD "C99"
#else
#  define IOT_VER_C_STD "C89"
#endif

/* ------------------------------ 特性位 ------------------------------ */

#define IOT_VERSION_FEATURES                                        \
    ((IOT_USE_TRACE            ? IOT_FEATURE_TRACE          : 0UL) | \
     (IOT_ASSERT_ENABLE        ? IOT_FEATURE_ASSERT         : 0UL) | \
     (IOT_CRASH_ENABLE         ? IOT_FEATURE_CRASH          : 0UL) | \
     (IOT_USE_STACK_CHECK      ? IOT_FEATURE_STACK_CHECK    : 0UL) | \
     (IOT_USE_PRIORITY_INHERIT ? IOT_FEATURE_PRIO_INHERIT   : 0UL) | \
     (IOT_USE_TIME_SLICING     ? IOT_FEATURE_TIME_SLICING   : 0UL) | \
     (IOT_USE_STATS            ? IOT_FEATURE_STATS          : 0UL) | \
     (IOT_USE_EVENT            ? IOT_FEATURE_EVENT          : 0UL) | \
     (IOT_USE_SBUF             ? IOT_FEATURE_SBUF           : 0UL) | \
     (IOT_USE_MSGQ             ? IOT_FEATURE_MSGQ           : 0UL) | \
     (IOT_USE_NOTIFY           ? IOT_FEATURE_NOTIFY         : 0UL) | \
     (IOT_USE_TICKLESS         ? IOT_FEATURE_TICKLESS       : 0UL) | \
     (IOT_USE_TIMER_TASK       ? IOT_FEATURE_TIMER_TASK     : 0UL))

/* ------------------------------ 描述符 ------------------------------ */

static const iot_version_info_t s_version = {
    IOT_VERSION_MAJOR,
    IOT_VERSION_MINOR,
    IOT_VERSION_PATCH,
    IOT_VERSION_SEMVER,
    IOT_VERSION_PRERELEASE,
    IOT_VERSION_BUILD,
    IOT_PROJECT_NAME,
    IOT_LICENSE_NAME,
    IOT_LICENSE_SPDX,
    IOT_LICENSE_COPYRIGHT,
    __DATE__ " " __TIME__,
    IOT_VER_PLATFORM,
    IOT_VER_COMPILER,
    IOT_VER_C_STD,
    (uint32_t)IOT_TICK_HZ,
    (uint16_t)IOT_MAX_PRIORITY,
    (uint32_t)IOT_VERSION_FEATURES
};

/* ------------------------------ 接口 ------------------------------ */

const iot_version_info_t *iot_version_get(void)
{
    return &s_version;
}

const char *iot_version_string(void)
{
    return IOT_VERSION_SEMVER;
}

const char *iot_build_datetime(void)
{
    return __DATE__ " " __TIME__;
}

bool iot_version_has_feature(uint32_t feature)
{
    return (feature != 0U) && ((s_version.features & feature) == feature);
}

/* 把特性位图渲染成空格分隔的名字列表 */
static void iot_version_features_text(uint32_t features, char *buf, size_t cap)
{
    static const struct {
        uint32_t    bit;
        const char *name;
    } table[] = {
        { IOT_FEATURE_TRACE,        "trace" },
        { IOT_FEATURE_ASSERT,       "assert" },
        { IOT_FEATURE_CRASH,        "crash" },
        { IOT_FEATURE_STACK_CHECK,  "stack_check" },
        { IOT_FEATURE_PRIO_INHERIT, "prio_inherit" },
        { IOT_FEATURE_TIME_SLICING, "time_slice" },
        { IOT_FEATURE_STATS,        "stats" },
        { IOT_FEATURE_EVENT,        "event" },
        { IOT_FEATURE_SBUF,         "sbuf" },
        { IOT_FEATURE_MSGQ,         "msgq" },
        { IOT_FEATURE_NOTIFY,       "notify" },
        { IOT_FEATURE_TICKLESS,     "tickless" },
        { IOT_FEATURE_TIMER_TASK,   "timer_task" }
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

int iot_version_format(char *buf, size_t cap)
{
    char features[192];
    const iot_version_info_t *v = &s_version;
    const char *pre;
    const char *bld;
    int n;

    if (buf == NULL || cap == 0U) {
        return -1;
    }

    iot_version_features_text(v->features, features, sizeof(features));
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
