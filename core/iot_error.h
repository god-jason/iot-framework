#ifndef IOT_ERROR_H
#define IOT_ERROR_H

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 将错误码转为可读字符串（用于日志与诊断） */
const char *iot_error_str(iot_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* IOT_ERROR_H */