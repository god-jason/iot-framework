#include "log.h"
#include "common.h"

#include <stdio.h>

/* 全局运行期日志等级，默认 INFO */
int g_log_level = LOG_LEVEL;

/* 每行最多 16 字节 */
#define LOG_HEX_PER_LINE 16U

/* 等级数值 -> 单字符前缀 */
static char log_level_char(int level)
{
    switch (level) {
    case LOG_LEVEL_DEBUG:
        return 'D';
    case LOG_LEVEL_INFO:
        return 'I';
    case LOG_LEVEL_WARN:
        return 'W';
    case LOG_LEVEL_ERROR:
        return 'E';
    case LOG_LEVEL_FATAL:
        return 'F';
    default:
        return '?';
    }
}

void log_hexdump(int level, const char *tag, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    char line[160];
    uint32_t offset;

    if (p == NULL || len == 0U) {
        return;
    }

    /* 整行拼到本地缓冲后一次性输出，避免逐段 ros_printf 被其他日志打断 */
    for (offset = 0U; offset < len; offset += LOG_HEX_PER_LINE) {
        int off = str_printf(line, sizeof(line), "[%c][%s] %04lX:",
                           log_level_char(level), (tag != NULL) ? tag : "-",
                           (unsigned long)offset);
        uint32_t i;

        if (off < 0) {
            off = 0;
        }
        if ((size_t)off >= sizeof(line)) {
            off = (int)sizeof(line) - 1;
        }
        for (i = 0U; i < LOG_HEX_PER_LINE && (offset + i) < len; i++) {
            int n = str_printf(&line[off], (uint32_t)(sizeof(line) - (size_t)off),
                             " %02X", (unsigned)p[offset + i]);

            if (n < 0 || (size_t)n >= sizeof(line) - (size_t)off) {
                break;   /* 缓冲将满，停止追加本行剩余字节 */
            }
            off += n;
        }
        if ((size_t)off + 2U < sizeof(line)) {
            line[off++] = '\r';
            line[off++] = '\n';
            line[off] = '\0';
        }
        ros_printf("%s", line);
    }
}
