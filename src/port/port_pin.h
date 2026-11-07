#ifndef PORT_PIN_H
#define PORT_PIN_H

/*
 * 引脚复用配置。GPIO 电气属性由 port_gpio 管理。
 *
 * 本模块只负责把逻辑引脚（port_pin_t）设置为 GPIO / 复用功能 / 模拟模式，
 * 不处理上下拉、速度等电气参数；那些参数请通过 port_gpio_init() 配置。
 * 引脚编码见 port_def.h 中的 PORT_PIN() 宏。
 */

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 引脚功能选择 */
typedef enum {
    PORT_PIN_FUNCTION_GPIO = 0,  /* 普通 GPIO，由 port_gpio 控制 */
    PORT_PIN_FUNCTION_AF,        /* 复用功能，需配合 alternate 编号 */
    PORT_PIN_FUNCTION_ANALOG     /* 模拟模式（ADC/DAC/COMP 等） */
} port_pin_function_t;

/* 单个引脚的复用配置 */
typedef struct {
    port_pin_t pin;              /* 逻辑引脚编码（PORT_PIN(port, pin)） */
    port_pin_function_t function;/* 目标功能 */
    uint8_t alternate;           /* 复用功能编号，仅 PORT_PIN_FUNCTION_AF 时有效 */
} port_pin_config_t;

/*
 * 初始化引脚复用。
 * config : 配置项，不可为 NULL；function 为 AF 时必须填写合法 alternate。
 * 返回   : PORT_OK 成功；config 为 NULL 返回 PORT_INVALID_PARAM；
 *          平台不支持该复用编号时返回 PORT_NOT_SUPPORTED。
 */
port_status_t port_pin_init(const port_pin_config_t *config);

/*
 * 复位引脚复用为默认状态（通常恢复为模拟/浮空输入）。
 * pin  : 逻辑引脚编码。
 * 返回 : PORT_OK 成功；引脚非法返回 PORT_INVALID_PARAM。
 */
port_status_t port_pin_deinit(port_pin_t pin);

#ifdef __cplusplus
}
#endif

#endif /* PORT_PIN_H */
