#ifndef HAL_RNG_H
#define HAL_RNG_H

/*
 * 硬件随机数发生器：获取单个随机数或填充缓冲区。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 HAL_INVALID_PARAM。
 * 熵源未就绪时返回 HAL_NOT_READY。
 */

#include "hal_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RNG 句柄 */
typedef struct hal_rng {
    hal_handle_base_t base;    /* 通用句柄基类，勿移动位置 */
} hal_rng_t;

/*
 * 初始化 RNG。
 * handle : 句柄，不可为 NULL。
 * 返回   : HAL_OK；HAL_INVALID_PARAM；平台无 RNG 返回 HAL_NOT_SUPPORTED。
 */
int hal_rng_init(hal_rng_t *handle);

/* 反初始化 RNG；返回 HAL_OK / HAL_INVALID_PARAM */
int hal_rng_deinit(hal_rng_t *handle);

/*
 * 获取一个 32 位随机数。
 * value : 输出，不可为 NULL。
 * 返回  : HAL_OK；HAL_INVALID_PARAM；熵源未就绪返回 HAL_NOT_READY。
 */
int hal_rng_get(hal_rng_t *handle, uint32_t *value);

/*
 * 用随机数填充缓冲区。
 * buffer : 目标缓冲区，不可为 NULL；
 * length : 字节数，内部按 32 位字填充，尾部可能不足 4 字节。
 * 返回   : HAL_OK；HAL_INVALID_PARAM；HAL_NOT_READY。
 */
int hal_rng_generate(hal_rng_t *handle, void *buffer, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* HAL_RNG_H */
