#ifndef IOT_LOGGER_H
#define IOT_LOGGER_H

/*
 * 多级日志系统。可通过注册 backend 将日志输出到 UART、存储或远程。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IOT_LOG_DEBUG = 0,
    IOT_LOG_INFO  = 1,
    IOT_LOG_WARN  = 2,
    IOT_LOG_ERROR = 3,
    IOT_LOG_FATAL = 4
} iot_log_level_t;

typedef void (*iot_log_output_fn)(iot_log_level_t level,
                                  const char *tag,
                                  const char *message,
                                  void *user);

typedef struct {
    iot_log_level_t level;   /* 该 backend 最低输出等级 */
    const char *tag;         /* 模块标签（可空） */
    iot_log_output_fn output;
    void *user;
} iot_log_backend_t;

int iot_log_init(void);
int iot_log_register(const iot_log_backend_t *backend);
int iot_log_unregister(iot_log_output_fn output);

int iot_log_set_level(iot_log_level_t level);
iot_log_level_t iot_log_get_level(void);

void iot_log_write(iot_log_level_t level,
                   const char *tag,
                   const char *fmt, ...);

void iot_log_hexdump(iot_log_level_t level,
                     const char *tag,
                     const void *data,
                     uint32_t len);

#define LOG_D(tag, ...) iot_log_write(IOT_LOG_DEBUG, tag, __VA_ARGS__)
#define LOG_I(tag, ...) iot_log_write(IOT_LOG_INFO,  tag, __VA_ARGS__)
#define LOG_W(tag, ...) iot_log_write(IOT_LOG_WARN,  tag, __VA_ARGS__)
#define LOG_E(tag, ...) iot_log_write(IOT_LOG_ERROR, tag, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* IOT_LOGGER_H */