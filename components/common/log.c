#include "log.h"

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
    uint32_t offset;

    if (p == NULL || len == 0U) {
        return;
    }

    for (offset = 0U; offset < len; offset += LOG_HEX_PER_LINE) {
        uint32_t i;

        ros_printf("[%c][%s] %04lX:", log_level_char(level),
                   (tag != NULL) ? tag : "-", (unsigned long)offset);
        for (i = 0U; i < LOG_HEX_PER_LINE && (offset + i) < len; i++) {
            ros_printf(" %02X", (unsigned)p[offset + i]);
        }
        ros_printf("\r\n");
    }
}
