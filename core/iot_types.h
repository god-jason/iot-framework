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

/* 统一错误码 */
#define IOT_ERR                ((iot_err_t)-1)
#define IOT_ERR_PARAM          ((iot_err_t)-2)
#define IOT_ERR_NOMEM          ((iot_err_t)-3)
#define IOT_ERR_BUSY           ((iot_err_t)-4)
#define IOT_ERR_TIMEOUT        ((iot_err_t)-5)
#define IOT_ERR_STATE          ((iot_err_t)-6)
#define IOT_ERR_IO             ((iot_err_t)-7)
#define IOT_ERR_CRC            ((iot_err_t)-8)
#define IOT_ERR_AUTH           ((iot_err_t)-9)
#define IOT_ERR_NOT_FOUND      ((iot_err_t)-10)
#define IOT_ERR_NOT_SUPPORTED  ((iot_err_t)-11)
#define IOT_ERR_OVERFLOW       ((iot_err_t)-12)
#define IOT_ERR_INVALID        ((iot_err_t)-13)
#define IOT_ERR_AGAIN          ((iot_err_t)-14)
#define IOT_ERR_EXISTS         ((iot_err_t)-15)
#define IOT_ERR_CANCEL         ((iot_err_t)-16)

#ifdef __cplusplus
}
#endif

#endif /* IOT_TYPES_H */