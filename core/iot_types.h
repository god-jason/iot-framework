#ifndef IOT_TYPES_H
#define IOT_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 统一基础标量类型 */
typedef int32_t  iot_err_t;   /* 返回码 */
typedef uint32_t iot_tick_t;  /* 系统 Tick（单调递增） */
typedef uint32_t iot_size_t;  /* 长度 / 容量 */
typedef uint32_t iot_id_t;    /* 对象 ID */

/* 通用无参回调 */
typedef void (*iot_callback_fn)(void *user);

#define IOT_NULL NULL
#define IOT_OK   ((iot_err_t)0)

#ifdef __cplusplus
}
#endif

#endif /* IOT_TYPES_H */