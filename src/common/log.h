#ifndef LOG_H
#define LOG_H

/*
 * 轻量日志宏。
 *
 * 设计：
 *   - 对外只有 LOG_D/LOG_I/LOG_W/LOG_E/LOG_F/LOG_EF 与 LOG_HEXDUMP 宏；
 *   - tag 必须是字符串字面量（或字符串宏），在编译期拼接到格式串；
 *   - 运行期等级保存在 g_log_level，默认 LOG_LEVEL_INFO，
 *     可用 log_set_level()/log_get_level() 宏读写；
 *   - 输出统一走 ros_printf（任务 stdio），未绑定/未初始化时由 ros_io
 *     的默认实现决定（通常为平台默认：PC stdout / MCU UART）；
 *   - 十六进制转储的逐行格式化放在 log.c 中，避免宏体过于臃肿。
 *
 * 默认等级为 INFO；可在编译选项中定义 LOG_LEVEL 覆盖，例如
 *   -DLOG_LEVEL=LOG_LEVEL_DEBUG
 */

#include <stdint.h>

#include "ros_io.h"

/* 日志等级：数值越大越严重；LOG_LEVEL_NONE 用于关闭全部日志 */
#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_ERROR 3
#define LOG_LEVEL_FATAL 4
#define LOG_LEVEL_NONE  5

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

/* 全局日志等级（定义在 log.c），可通过 log_set_level 宏修改 */
extern int g_log_level;

/*
 * 核心输出宏：tag 必须是字符串字面量（或字符串宏），
 * 在编译期与格式串拼接，减少运行时开销。
 */
#define LOG_OUTPUT(level, tag, ...)                        \
    do {                                                   \
        ros_printf("[" level "][" tag "] " __VA_ARGS__);   \
        ros_printf("\r\n");                                \
    } while (0)

#define LOG_D(tag, ...)                                \
    do {                                               \
        if (g_log_level <= LOG_LEVEL_DEBUG) {          \
            LOG_OUTPUT("D", tag, __VA_ARGS__);         \
        }                                              \
    } while (0)
#define LOG_I(tag, ...)                                \
    do {                                               \
        if (g_log_level <= LOG_LEVEL_INFO) {           \
            LOG_OUTPUT("I", tag, __VA_ARGS__);         \
        }                                              \
    } while (0)
#define LOG_W(tag, ...)                                \
    do {                                               \
        if (g_log_level <= LOG_LEVEL_WARN) {           \
            LOG_OUTPUT("W", tag, __VA_ARGS__);         \
        }                                              \
    } while (0)
#define LOG_E(tag, ...)                                \
    do {                                               \
        if (g_log_level <= LOG_LEVEL_ERROR) {          \
            LOG_OUTPUT("E", tag, __VA_ARGS__);         \
        }                                              \
    } while (0)
#define LOG_F(tag, ...)                                \
    do {                                               \
        if (g_log_level <= LOG_LEVEL_FATAL) {          \
            LOG_OUTPUT("F", tag, __VA_ARGS__);         \
        }                                              \
    } while (0)
#define LOG_EF(tag, ...) LOG_F(tag, __VA_ARGS__)

/* 运行期等级控制（宏形式，直接读写全局变量） */
#define log_set_level(level) (g_log_level = (level))
#define log_get_level()      (g_log_level)

/* 十六进制转储：每行 16 字节，前缀为行首偏移（4 位十六进制） */
#define LOG_HEXDUMP(level, tag, data, len)                               \
    do {                                                                 \
        if ((level) >= g_log_level) {                                    \
            log_hexdump((level), (tag), (data), (len));                  \
        }                                                                \
    } while (0)

/*
 * 十六进制转储实现（唯一非宏接口）。
 * 参数：level - LOG_LEVEL_* 等级；tag - 模块标签，可为 NULL；
 *       data - 待打印数据；len - 字节数；data 为 NULL 或 len 为 0 时直接返回。
 */
void log_hexdump(int level, const char *tag, const void *data, uint32_t len);

#endif /* LOG_H */
