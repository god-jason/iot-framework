#include "log.h"

#include <stdio.h>

/* 全局运行期日志等级，默认 INFO */
int g_log_level = LOG_LEVEL;

/* 每行最多 16 字节，每个字节 "XX " 占 3 字符，另留结束符 */
#define LOG_HEX_LINE_MAX (16U * 3U + 1U)
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
    char line[LOG_HEX_LINE_MAX];
    uint32_t offset;

    if (p == NULL || len == 0U) {
        return;
    }

    for (offset = 0U; offset < len; offset += LOG_HEX_PER_LINE) {
        uint32_t i;
        uint32_t pos = 0U;

        line[0] = '\0';
        for (i = 0U; i < LOG_HEX_PER_LINE && (offset + i) < len; i++) {
            int n = snprintf(&line[pos], sizeof(line) - pos, "%02X ",
                             (unsigned)p[offset + i]);
            if (n <= 0) {
                break;
            }
            pos += (uint32_t)n;
        }

        printf("[%c][%s] %04lX: %s\r\n", log_level_char(level),
               (tag != NULL) ? tag : "-", (unsigned long)offset, line);
    }
}
