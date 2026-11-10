/*
 * stm32f4xx_hal_conf.h - STM32CubeF4 HAL 裁剪配置（STM32F407VET6）
 *
 * 只启用本板实际用到的模块：CORTEX / DMA / FLASH / GPIO / I2C / IWDG /
 * PWR / RCC / RTC / SPI / TIM / UART / WWDG / ADC / CAN / DAC / RNG。
 * platforms/stm32/stm32_f4.c 中的 ETH / SDIO / QSPI / USB 均为桩实现，
 * 不依赖对应 Cube 模块。
 */

#ifndef STM32F4XX_HAL_CONF_H
#define STM32F4XX_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 模块开关 ---------------- */
#define HAL_MODULE_ENABLED
#define HAL_ADC_MODULE_ENABLED
#define HAL_CAN_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DAC_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#define HAL_I2S_MODULE_ENABLED
#define HAL_IWDG_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_RNG_MODULE_ENABLED
#define HAL_RTC_MODULE_ENABLED
#define HAL_SPI_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_WWDG_MODULE_ENABLED

/* ---------------- 振荡器与电源参数 ---------------- */
#if !defined(HSE_VALUE)
#define HSE_VALUE               8000000U    /* 8MHz 外部晶振 */
#endif
#if !defined(HSE_STARTUP_TIMEOUT)
#define HSE_STARTUP_TIMEOUT     100U        /* ms */
#endif
#if !defined(HSI_VALUE)
#define HSI_VALUE               16000000U   /* 内部 RC */
#endif
#if !defined(LSI_VALUE)
#define LSI_VALUE               32000U
#endif
#if !defined(LSE_VALUE)
#define LSE_VALUE               32768U      /* 32.768kHz，未安装时仅 RTC 受影响 */
#endif
#if !defined(LSE_STARTUP_TIMEOUT)
#define LSE_STARTUP_TIMEOUT     5000U
#endif
#if !defined(EXTERNAL_CLOCK_VALUE)
#define EXTERNAL_CLOCK_VALUE    12288000U
#endif
#if !defined(VDD_VALUE)
#define VDD_VALUE               3300U
#endif

/* SysTick 优先级：与 PendSV 同为最低，避免打断其它 ISR */
#if !defined(TICK_INT_PRIORITY)
#define TICK_INT_PRIORITY       15U
#endif

#if !defined(USE_RTOS)
#define USE_RTOS                0U
#endif

/* HAL_Init() 打开的加速选项 */
#if !defined(PREFETCH_ENABLE)
#define PREFETCH_ENABLE         1U
#endif
#if !defined(INSTRUCTION_CACHE_ENABLE)
#define INSTRUCTION_CACHE_ENABLE 1U
#endif
#if !defined(DATA_CACHE_ENABLE)
#define DATA_CACHE_ENABLE       1U
#endif

#define USE_SPI_CRC             0U

/* ---------------- 参数断言 ---------------- */
/* #define USE_FULL_ASSERT    1U */

#ifdef USE_FULL_ASSERT
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
void assert_failed(uint8_t *file, uint32_t line);
#else
#define assert_param(expr) ((void)0U)
#endif

/* ---------------- 模块头文件 ---------------- */
#ifdef HAL_RCC_MODULE_ENABLED
#include "stm32f4xx_hal_rcc.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
#include "stm32f4xx_hal_gpio.h"
#endif
#ifdef HAL_DMA_MODULE_ENABLED
#include "stm32f4xx_hal_dma.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
#include "stm32f4xx_hal_cortex.h"
#endif
#ifdef HAL_ADC_MODULE_ENABLED
#include "stm32f4xx_hal_adc.h"
#endif
#ifdef HAL_CAN_MODULE_ENABLED
#include "stm32f4xx_hal_can.h"
#endif
#ifdef HAL_DAC_MODULE_ENABLED
#include "stm32f4xx_hal_dac.h"
#endif
#ifdef HAL_FLASH_MODULE_ENABLED
#include "stm32f4xx_hal_flash.h"
#endif
#ifdef HAL_I2C_MODULE_ENABLED
#include "stm32f4xx_hal_i2c.h"
#endif
#ifdef HAL_I2S_MODULE_ENABLED
#include "stm32f4xx_hal_i2s.h"
#endif
#ifdef HAL_IWDG_MODULE_ENABLED
#include "stm32f4xx_hal_iwdg.h"
#endif
#ifdef HAL_PWR_MODULE_ENABLED
#include "stm32f4xx_hal_pwr.h"
#endif
#ifdef HAL_RNG_MODULE_ENABLED
#include "stm32f4xx_hal_rng.h"
#endif
#ifdef HAL_RTC_MODULE_ENABLED
#include "stm32f4xx_hal_rtc.h"
#endif
#ifdef HAL_SPI_MODULE_ENABLED
#include "stm32f4xx_hal_spi.h"
#endif
#ifdef HAL_TIM_MODULE_ENABLED
#include "stm32f4xx_hal_tim.h"
#endif
#ifdef HAL_UART_MODULE_ENABLED
#include "stm32f4xx_hal_uart.h"
#endif
#ifdef HAL_WWDG_MODULE_ENABLED
#include "stm32f4xx_hal_wwdg.h"
#endif

#ifdef __cplusplus
}
#endif

#endif /* STM32F4XX_HAL_CONF_H */
