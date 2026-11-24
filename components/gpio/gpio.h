#ifndef GPIO_H
#define GPIO_H

/*
 * gpio.h - GPIO Shell 命令
 *
 * 注册 gpio 命令到 cmd 全局命令表：
 *   gpio mode <pin> in|out [pull|level]
 *   gpio set <pin> <0|1>
 *   gpio get <pin>
 *   gpio toggle <pin>
 *   gpio deinit <pin>
 *
 * 引脚格式：PA5 / pb12 / 0x0105（HAL_PIN 原始编码）。
 */

#ifdef __cplusplus
extern "C" {
#endif

/* 注册 gpio 命令（幂等）；返回 0 成功，-1 失败 */
int gpio_cmd_register(void);

#ifdef __cplusplus
}
#endif

#endif /* GPIO_H */
