#ifndef IOT_RNG_H
#define IOT_RNG_H

/*
 * 硬件随机数发生器：获取单个随机数或填充缓冲区。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 IOT_INVALID_PARAM。
 * 熵源未就绪时返回 IOT_NOT_READY。
 */

#include "iot_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RNG 句柄 */
typedef struct iot_rng {
    iot_handle_base_t base;    /* 通用句柄基类，勿移动位置 */
} iot_rng_t;

/*
 * 初始化 RNG。
 * handle : 句柄，不可为 NULL。
 * 返回   : IOT_OK；IOT_INVALID_PARAM；平台无 RNG 返回 IOT_NOT_SUPPORTED。
 */
iot_status_t iot_rng_init(iot_rng_t *handle);

/* 反初始化 RNG；返回 IOT_OK / IOT_INVALID_PARAM */
iot_status_t iot_rng_deinit(iot_rng_t *handle);

/*
 * 获取一个 32 位随机数。
 * value : 输出，不可为 NULL。
 * 返回  : IOT_OK；IOT_INVALID_PARAM；熵源未就绪返回 IOT_NOT_READY。
 */
iot_status_t iot_rng_get(iot_rng_t *handle, uint32_t *value);

/*
 * 用随机数填充缓冲区。
 * buffer : 目标缓冲区，不可为 NULL；
 * length : 字节数，内部按 32 位字填充，尾部可能不足 4 字节。
 * 返回   : IOT_OK；IOT_INVALID_PARAM；IOT_NOT_READY。
 */
iot_status_t iot_rng_generate(iot_rng_t *handle, void *buffer, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* IOT_RNG_H */
