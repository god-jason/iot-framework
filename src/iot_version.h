#ifndef IOT_VERSION_H
#define IOT_VERSION_H

/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 iot-os contributors
 *
 * iot_version.h - 版本 / 构建时间 / 编译配置 / 开源许可证信息
 *
 * 版本号遵循 Semantic Versioning 2.0.0：
 *     MAJOR.MINOR.PATCH[-PRERELEASE][+BUILD]
 *
 * 编译时间取自 iot_version.c 的 __DATE__ / __TIME__，合并为一个字符串，
 * 即该文件最近一次参与编译的时刻（增量构建未重新编译本文件时不会刷新）。
 *
 * 所有描述均为静态常量字符串（.rodata），运行期零 RAM 占用；
 * iot_version_get() 返回只读描述符，iot_version_format() 输出多行摘要。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================== 版本号（semver） ============================== */

/* 修改版本号时，请同步 xmake.lua 的 set_version("x.y.z") */

#ifndef IOT_VERSION_MAJOR
#define IOT_VERSION_MAJOR       1
#endif
#ifndef IOT_VERSION_MINOR
#define IOT_VERSION_MINOR       0
#endif
#ifndef IOT_VERSION_PATCH
#define IOT_VERSION_PATCH       0
#endif

/* 预发布标记，例如 "rc1"；空串表示正式版 */
#ifndef IOT_VERSION_PRERELEASE
#define IOT_VERSION_PRERELEASE  ""
#endif
/* 构建元数据，例如 git 短哈希；空串表示未提供 */
#ifndef IOT_VERSION_BUILD
#define IOT_VERSION_BUILD       ""
#endif

#define IOT_VERSION_STR_(x)     #x
#define IOT_VERSION_STR(x)      IOT_VERSION_STR_(x)

/* 由 MAJOR/MINOR/PATCH 自动拼出的 semver 主体 */
#define IOT_VERSION_SEMVER                                  \
    IOT_VERSION_STR(IOT_VERSION_MAJOR) "."                  \
    IOT_VERSION_STR(IOT_VERSION_MINOR) "."                  \
    IOT_VERSION_STR(IOT_VERSION_PATCH)

/* ============================== 项目 / 许可证 ============================== */

#define IOT_PROJECT_NAME        "iot-os"
#define IOT_LICENSE_NAME        "MIT"
#define IOT_LICENSE_SPDX        "MIT"
#define IOT_LICENSE_COPYRIGHT   "Copyright (c) 2026 iot-os contributors"

/* ============================== 编译期特性位 ============================== */

#define IOT_FEATURE_TRACE          (1UL << 0)    /* 事件跟踪 */
#define IOT_FEATURE_TRACE_CTF      (1UL << 1)    /* barectf/CTF 后端 */
#define IOT_FEATURE_ASSERT         (1UL << 2)    /* 断言 */
#define IOT_FEATURE_CRASH          (1UL << 3)    /* 崩溃现场记录 */
#define IOT_FEATURE_STACK_CHECK    (1UL << 4)    /* 栈溢出检测 */
#define IOT_FEATURE_PRIO_INHERIT   (1UL << 5)    /* 互斥量优先级继承 */
#define IOT_FEATURE_TIME_SLICING   (1UL << 6)    /* 同优先级时间片 */
#define IOT_FEATURE_STATS          (1UL << 7)    /* 运行时统计 */
#define IOT_FEATURE_EVENT          (1UL << 8)    /* 事件组 */
#define IOT_FEATURE_SBUF           (1UL << 9)    /* 流式缓冲 */
#define IOT_FEATURE_MSGQ           (1UL << 10)   /* 变长消息队列 */
#define IOT_FEATURE_NOTIFY         (1UL << 11)   /* 任务通知 */
#define IOT_FEATURE_TICKLESS       (1UL << 12)   /* tickless 空闲 */
#define IOT_FEATURE_TIMER_TASK     (1UL << 13)   /* 定时器回调独立任务 */

/* ============================== 描述符 ============================== */

typedef struct {
    uint16_t    major;
    uint16_t    minor;
    uint16_t    patch;
    const char *semver;         /* "1.0.0"（不含 prerelease/build） */
    const char *prerelease;     /* 可空串 */
    const char *build;          /* 可空串 */
    const char *project;        /* "iot-os" */
    const char *license;        /* "MIT" */
    const char *license_spdx;   /* "MIT" */
    const char *copyright;      /* 版权声明 */
    const char *datetime;       /* 编译时间 "__DATE__ __TIME__" */
    const char *platform;       /* STM32F1 / Windows / Linux … */
    const char *compiler;       /* GCC / Clang / MSVC + 版本 */
    const char *c_standard;     /* C99 / C11 … */
    uint32_t    tick_hz;        /* IOT_TICK_HZ */
    uint16_t    max_priority;   /* IOT_MAX_PRIORITY */
    uint32_t    features;       /* IOT_FEATURE_* 位图 */
} iot_version_info_t;

/* ============================== 接口 ============================== */

/* 只读版本描述符（静态存储，无需释放） */
const iot_version_info_t *iot_version_get(void);

/* 便捷取值 */
const char *iot_version_string(void);     /* semver 主体 */
const char *iot_build_datetime(void);     /* "__DATE__ __TIME__" */
bool iot_version_has_feature(uint32_t feature);

/*
 * 输出多行版本摘要（版本 / 编译时间 / 平台 / 编译器 / 内核参数 /
 * 许可证 / 版权 / 特性位）。buf 按 snprintf 语义截断并保证以 '\0' 结尾。
 * 返回写入字符数（不含结尾 '\0'）；参数非法返回 -1。
 */
int iot_version_format(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* IOT_VERSION_H */
