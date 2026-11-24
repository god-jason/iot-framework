/*
 * stm32_config.c - STM32 配置存储板级门面（F1/F4 共用）
 *
 * 存储逻辑位于 boot_config.c（双区轮转追加日志）与 components/config。
 * 本文件只暴露通用句柄，应用可直接调用 config_store_load/save/format。
 */

#include "boot_config.h"
#include "config.h"

config_store_t *stm32_config_storage(void);

config_store_t *stm32_config_storage(void)
{
    return boot_config_storage();
}
