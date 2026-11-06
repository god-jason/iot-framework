#ifndef IOT_DIAG_H
#define IOT_DIAG_H

/*
 * 诊断记录（环形缓冲）：记录运行错误/警告，便于远程分析设备故障。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IOT_DIAG_INFO,
    IOT_DIAG_WARNING,
    IOT_DIAG_ERROR,
    IOT_DIAG_FATAL
} iot_diag_level_t;

typedef struct {
    uint32_t code;
    iot_diag_level_t level;

    const char *module;
    const char *message;

    iot_tick_t timestamp;
} iot_diag_record_t;

int iot_diag_init(void);

int iot_diag_report(uint32_t code, iot_diag_level_t level,
                    const char *module, const char *message);

int iot_diag_get(uint32_t index, iot_diag_record_t *record);
int iot_diag_clear(void);
uint32_t iot_diag_count(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_DIAG_H */