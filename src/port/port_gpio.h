#ifndef PORT_GPIO_H
#define PORT_GPIO_H

/*
 * GPIO 电气配置与读写。
 * 引脚用 port_pin_t 逻辑编码（PORT_PIN(port, pin)），端口映射由平台完成；
 * 复用功能选择请配合 port_pin_init() 使用。
 *
 * 所有接口在引脚非法时返回 PORT_INVALID_PARAM。
 * 读写操作假定引脚已配置为对应方向，本模块不做模式检查。
 */

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 引脚电平 */
typedef enum {
    PORT_GPIO_LOW = 0,      /* 低电平 */
    PORT_GPIO_HIGH          /* 高电平 */
} port_gpio_level_t;

/* 引脚模式 */
typedef enum {
    PORT_GPIO_MODE_INPUT = 0,   /* 输入 */
    PORT_GPIO_MODE_OUTPUT,      /* 推挽/开漏输出 */
    PORT_GPIO_MODE_ANALOG,      /* 模拟，供 ADC/DAC 使用 */
    PORT_GPIO_MODE_AF           /* 复用功能 */
} port_gpio_mode_t;

/* 输出类型（仅输出模式有效） */
typedef enum {
    PORT_GPIO_OUTPUT_PP = 0,    /* 推挽 */
    PORT_GPIO_OUTPUT_OD         /* 开漏，需外部上拉 */
} port_gpio_output_t;

/* 内部上下拉 */
typedef enum {
    PORT_GPIO_NOPULL = 0,       /* 浮空 */
    PORT_GPIO_PULLUP,           /* 上拉 */
    PORT_GPIO_PULLDOWN          /* 下拉 */
} port_gpio_pull_t;

/* 输出速度（翻转速率，越高EMI越大） */
typedef enum {
    PORT_GPIO_SPEED_LOW = 0,    /* 低速 */
    PORT_GPIO_SPEED_MEDIUM,     /* 中速 */
    PORT_GPIO_SPEED_HIGH,       /* 高速 */
    PORT_GPIO_SPEED_VERY_HIGH   /* 极高速 */
} port_gpio_speed_t;

/* GPIO 初始化配置 */
typedef struct {
    port_gpio_mode_t mode;          /* 引脚模式 */
    port_gpio_output_t output;      /* 输出类型，仅输出模式有效 */
    port_gpio_pull_t pull;          /* 内部上下拉 */
    port_gpio_speed_t speed;        /* 输出速度 */
    uint8_t alternate;              /* 复用功能编号，仅 MODE_AF 时有效 */
    port_gpio_level_t initial_level;/* 初始化后的初始电平，仅输出模式有效 */
} port_gpio_config_t;

/*
 * 初始化引脚。
 * pin    : 逻辑引脚编码。
 * config : 配置项，不可为 NULL。
 * 返回   : PORT_OK 成功；PORT_INVALID_PARAM；不支持的模式返回 PORT_NOT_SUPPORTED。
 */
port_status_t port_gpio_init(port_pin_t pin, const port_gpio_config_t *config);

/*
 * 复位引脚到默认状态（通常为浮空输入）。
 * pin  : 逻辑引脚编码。
 * 返回 : PORT_OK 成功；PORT_INVALID_PARAM。
 */
port_status_t port_gpio_deinit(port_pin_t pin);

/*
 * 写引脚电平。
 * pin   : 逻辑引脚编码，需已配置为输出。
 * level : 目标电平。
 * 返回  : PORT_OK 成功；PORT_INVALID_PARAM。
 */
port_status_t port_gpio_write(port_pin_t pin, port_gpio_level_t level);

/*
 * 读引脚电平。
 * pin   : 逻辑引脚编码。
 * level : 输出参数，不可为 NULL。
 * 返回  : PORT_OK 成功；pin/level 非法返回 PORT_INVALID_PARAM。
 */
port_status_t port_gpio_read(port_pin_t pin, port_gpio_level_t *level);

/*
 * 翻转引脚电平。
 * pin  : 逻辑引脚编码，需已配置为输出。
 * 返回 : PORT_OK 成功；PORT_INVALID_PARAM。
 * 注意 : 实现上为读改写，非原子，多中断并发操作同一引脚需加保护。
 */
port_status_t port_gpio_toggle(port_pin_t pin);

/* 置高电平，等价于 write(pin, PORT_GPIO_HIGH) */
port_status_t port_gpio_set(port_pin_t pin);

/* 置低电平，等价于 write(pin, PORT_GPIO_LOW) */
port_status_t port_gpio_reset(port_pin_t pin);

#ifdef __cplusplus
}
#endif

#endif /* PORT_GPIO_H */
