#ifndef IOT_STM32_CUBE_H
#define IOT_STM32_CUBE_H

/*
 * stm32_cube.h - STM32Cube HAL 统一包含入口（F1 / F4 / H7）
 *
 * 名称冲突说明：
 *   框架 hal/hal_status.h 用匿名枚举定义了 HAL_OK / HAL_ERROR / HAL_BUSY /
 *   HAL_TIMEOUT（成功为 0，错误为负值）；STM32Cube 的 stm32fxxxx_hal_def.h
 *   用同名枚举定义了 HAL_StatusTypeDef（0..3）。两者出现在同一编译单元会
 *   触发 "redeclaration of enumerator"。
 *
 *   因此这里在包含 Cube 头文件前把四个名字临时映射为 CUBE_HAL_*，包含完成
 *   后恢复，框架代码继续使用自己的 HAL_* 常量。需要判断 Cube 返回值的代码
 *   请使用 CUBE_HAL_OK 等，或调用 iot_cube_status()。
 *
 * 使用方式：
 *   #include "hal.h"
 *   #include "stm32_cube.h"      // 必须在 hal.h 之后
 */

#define HAL_OK      CUBE_HAL_OK
#define HAL_ERROR   CUBE_HAL_ERROR
#define HAL_BUSY    CUBE_HAL_BUSY
#define HAL_TIMEOUT CUBE_HAL_TIMEOUT

/* I2C 主机/从机模式：Cube 亦使用 HAL_I2C_MODE_* 命名 */
#define HAL_I2C_MODE_MASTER CUBE_HAL_I2C_MODE_MASTER
#define HAL_I2C_MODE_SLAVE  CUBE_HAL_I2C_MODE_SLAVE

#if defined(IOT_STM32_H7)
#include "stm32h7xx_hal.h"
#elif defined(IOT_STM32_F1)
#include "stm32f1xx_hal.h"
#elif defined(IOT_STM32_F4)
#include "stm32f4xx_hal.h"
#else
#include "stm32f4xx_hal.h"
#endif

#undef HAL_OK
#undef HAL_ERROR
#undef HAL_BUSY
#undef HAL_TIMEOUT
#undef HAL_I2C_MODE_MASTER
#undef HAL_I2C_MODE_SLAVE

#if defined(IOT_STM32_F4)
/* Cube F4 HAL 1.28 起把 DMA_MDATAALIGN_* 改名为 DMA_MEMORY_DATA_SIZE_*，
 * 早期版本（如 Keil DFP 2.17 携带的 1.27）用旧名：这里统一到新名。 */
#ifndef DMA_MEMORY_DATA_SIZE_BYTE
#define DMA_MEMORY_DATA_SIZE_BYTE       DMA_MDATAALIGN_BYTE
#endif
#ifndef DMA_MEMORY_DATA_SIZE_HALFWORD
#define DMA_MEMORY_DATA_SIZE_HALFWORD   DMA_MDATAALIGN_HALFWORD
#endif
#ifndef DMA_MEMORY_DATA_SIZE_WORD
#define DMA_MEMORY_DATA_SIZE_WORD       DMA_MDATAALIGN_WORD
#endif
#endif /* IOT_STM32_F4 */

#include "hal.h"

/* Cube HAL 状态码 -> 框架状态码 */
static inline int iot_cube_status(int status)
{
    switch (status) {
    case CUBE_HAL_OK:
        return HAL_OK;
    case CUBE_HAL_BUSY:
        return HAL_BUSY;
    case CUBE_HAL_TIMEOUT:
        return HAL_TIMEOUT;
    case CUBE_HAL_ERROR:
    default:
        return HAL_ERROR;
    }
}

#endif /* IOT_STM32_CUBE_H */
