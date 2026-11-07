#ifndef PORT_CORE_H
#define PORT_CORE_H

/*
 * PORT 核心接口：初始化、芯片信息、中断、系统控制、时间。
 * 具体实现由平台层提供。
 */

#include "port_def.h"
#include "port_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 / 反初始化（自动安装平台） */
port_status_t port_init(void);
port_status_t port_deinit(void);

/* 芯片信息 */
port_status_t port_get_info(port_mcu_info_t *info);

/* 中断管理 */
void port_irq_enable(void);
void port_irq_disable(void);

/* 系统控制 */
void port_system_reset(void);
void port_system_sleep(void);
void port_system_deepsleep(void);

/* 时间 */
uint32_t port_tick_get(void);
void port_delay_ms(uint32_t ms);
void port_delay_us(uint32_t us);

#ifdef __cplusplus
}
#endif

#endif /* PORT_CORE_H */
