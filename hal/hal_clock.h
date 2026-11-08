#ifndef HAL_CLOCK_H
#define HAL_CLOCK_H

/*
 * 时钟管理：配置系统时钟源与 PLL，查询各总线频率，控制外设时钟门控。
 * 具体时钟树由平台实现，公共接口只暴露与芯片无关的抽象。
 */

#include "hal_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 可查询的时钟总线 */
typedef enum {
    HAL_CLOCK_CORE = 0,    /* 内核时钟 */
    HAL_CLOCK_AHB,         /* AHB 总线时钟 */
    HAL_CLOCK_APB1,        /* APB1 总线时钟 */
    HAL_CLOCK_APB2,        /* APB2 总线时钟 */
    HAL_CLOCK_APB3,        /* APB3 总线时钟 */
    HAL_CLOCK_APB4         /* APB4 总线时钟 */
} hal_clock_bus_t;

/* 时钟源 */
typedef enum {
    HAL_CLOCK_HSI = 0,     /* 内部高速时钟 */
    HAL_CLOCK_HSE,         /* 外部高速时钟（晶振） */
    HAL_CLOCK_PLL,         /* 锁相环输出 */
    HAL_CLOCK_LSI,         /* 内部低速时钟 */
    HAL_CLOCK_LSE          /* 外部低速时钟 */
} hal_clock_source_t;

/* 时钟初始化配置 */
typedef struct {
    hal_clock_source_t source; /* 系统主时钟源 */
    uint32_t frequency;         /* 时钟源频率（Hz）；HSI/HSE 等输入频率 */
    uint32_t pll_frequency;     /* 期望的 PLL 输出频率（Hz），source 非 PLL 时忽略 */
} hal_clock_config_t;

/*
 * 按配置初始化系统时钟。
 * config : 配置项，不可为 NULL。
 * 返回   : HAL_OK 成功；HAL_INVALID_PARAM；时钟源不可用或倍频超限
 *          返回 HAL_NOT_SUPPORTED / HAL_HW_ERROR。
 * 注意   : 会改变系统主频，调用前应确认外设分频设置可接受。
 */
hal_status_t hal_clock_init(const hal_clock_config_t *config);

/*
 * 查询指定总线当前频率。
 * bus  : 总线类型；非法值返回 0。
 * 返回 : 频率（Hz）；平台未知时为 0，不会返回错误码。
 */
uint32_t hal_clock_get_frequency(hal_clock_bus_t bus);

/*
 * 使能外设时钟。
 * peripheral : 平台定义的外设标识（通常为寄存器基址或枚举值），不可为 NULL。
 * 返回       : HAL_OK 成功；HAL_INVALID_PARAM；平台不支持返回 HAL_NOT_SUPPORTED。
 * 注意       : 必须先使能时钟再访问外设寄存器。
 */
hal_status_t hal_clock_enable(void *peripheral);

/*
 * 关闭外设时钟。
 * peripheral : 平台定义的外设标识，不可为 NULL。
 * 返回       : HAL_OK 成功；HAL_INVALID_PARAM / HAL_NOT_SUPPORTED。
 * 注意       : 关闭后该外设寄存器访问将失败，正在传输的外设不可关闭。
 */
hal_status_t hal_clock_disable(void *peripheral);

#ifdef __cplusplus
}
#endif

#endif /* HAL_CLOCK_H */
