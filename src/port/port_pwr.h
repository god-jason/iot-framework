#ifndef PORT_PWR_H
#define PORT_PWR_H

/*
 * 电源与低功耗管理：进入睡眠/停止/待机模式、配置唤醒源，
 * 查询与清除复位/唤醒标志。
 *
 * PWR 为全局单实例资源，接口不接收句柄。
 * 进入低功耗后，唤醒通常从复位或断点继续，调用前应保存必要状态。
 */

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 低功耗模式 */
typedef enum {
    PORT_PWR_MODE_SLEEP = 0,    /* 睡眠：内核停止，外设继续运行 */
    PORT_PWR_MODE_STOP,         /* 停止：时钟停止，保留 SRAM 内容 */
    PORT_PWR_MODE_STANDBY       /* 待机：最低功耗，SRAM 内容通常丢失 */
} port_pwr_mode_t;

/* 唤醒源，按位或组合使用 */
typedef enum {
    PORT_PWR_WAKEUP_GPIO = 0,   /* GPIO 外部中断唤醒 */
    PORT_PWR_WAKEUP_RTC,        /* RTC 闹钟/唤醒定时器 */
    PORT_PWR_WAKEUP_WDG,        /* 看门狗唤醒 */
    PORT_PWR_WAKEUP_OTHER       /* 其他平台定制源 */
} port_pwr_wakeup_t;

/* 低功耗配置 */
typedef struct {
    port_pwr_mode_t mode;       /* 目标低功耗模式 */

    uint32_t wakeup_sources;    /* 唤醒源位掩码，每位对应一个 PORT_PWR_WAKEUP_* */

    bool low_power_regulator;   /* true 使用低功耗稳压器（部分模式有效） */
} port_pwr_config_t;

/* 复位/唤醒状态 */
typedef struct {
    uint32_t reset_flags;       /* 复位来源标志位，平台自定义编码 */
    uint32_t wakeup_flags;      /* 本次唤醒来源标志位，平台自定义编码 */
} port_pwr_status_t;

/*
 * 初始化电源管理模块。
 * 返回 : PORT_OK；平台不支持返回 PORT_NOT_SUPPORTED。
 */
port_status_t port_pwr_init(void);

/*
 * 进入指定低功耗模式。
 * config : 配置，不可为 NULL；wakeup_sources 为 0 时可能导致无法唤醒。
 * 返回   : PORT_OK 表示已唤醒返回；PORT_INVALID_PARAM；模式不支持 PORT_NOT_SUPPORTED。
 * 注意   : 该函数通常在被唤醒后才返回。
 */
port_status_t port_pwr_enter(const port_pwr_config_t *config);

/*
 * 查询复位与唤醒状态。
 * status : 输出结构，不可为 NULL。
 * 返回   : PORT_OK；PORT_INVALID_PARAM；未初始化返回 PORT_NOT_READY。
 */
port_status_t port_pwr_get_status(port_pwr_status_t *status);

/* 清除复位/唤醒标志；返回 PORT_OK / PORT_NOT_READY */
port_status_t port_pwr_clear_flags(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_PWR_H */
