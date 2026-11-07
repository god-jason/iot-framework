#ifndef IOT_GPIO_H
#define IOT_GPIO_H

/*
 * GPIO 电气配置与读写。
 * 引脚用 iot_pin_t 逻辑编码（IOT_PIN(port, pin)），端口映射由平台完成；
 * 复用功能选择请配合 iot_pin_init() 使用。
 *
 * 所有接口在引脚非法时返回 IOT_INVALID_PARAM。
 * 读写操作假定引脚已配置为对应方向，本模块不做模式检查。
 */

#include "iot_def.h"
#include "iot_pin.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 引脚电平 */
typedef enum {
    IOT_GPIO_LOW = 0,      /* 低电平 */
    IOT_GPIO_HIGH          /* 高电平 */
} iot_gpio_level_t;

/* 引脚模式 */
typedef enum {
    IOT_GPIO_MODE_INPUT = 0,   /* 输入 */
    IOT_GPIO_MODE_OUTPUT,      /* 推挽/开漏输出 */
    IOT_GPIO_MODE_ANALOG,      /* 模拟，供 ADC/DAC 使用 */
    IOT_GPIO_MODE_AF           /* 复用功能 */
} iot_gpio_mode_t;

/* 输出类型（仅输出模式有效） */
typedef enum {
    IOT_GPIO_OUTPUT_PP = 0,    /* 推挽 */
    IOT_GPIO_OUTPUT_OD         /* 开漏，需外部上拉 */
} iot_gpio_output_t;

/* 内部上下拉 */
typedef enum {
    IOT_GPIO_NOPULL = 0,       /* 浮空 */
    IOT_GPIO_PULLUP,           /* 上拉 */
    IOT_GPIO_PULLDOWN          /* 下拉 */
} iot_gpio_pull_t;

/* 输出速度（翻转速率，越高EMI越大） */
typedef enum {
    IOT_GPIO_SPEED_LOW = 0,    /* 低速 */
    IOT_GPIO_SPEED_MEDIUM,     /* 中速 */
    IOT_GPIO_SPEED_HIGH,       /* 高速 */
    IOT_GPIO_SPEED_VERY_HIGH   /* 极高速 */
} iot_gpio_speed_t;

/* GPIO 初始化配置 */
typedef struct {
    iot_gpio_mode_t mode;          /* 引脚模式 */
    iot_gpio_output_t output;      /* 输出类型，仅输出模式有效 */
    iot_gpio_pull_t pull;          /* 内部上下拉 */
    iot_gpio_speed_t speed;        /* 输出速度 */
    uint8_t alternate;              /* 复用功能编号，仅 MODE_AF 时有效 */
    iot_gpio_level_t initial_level;/* 初始化后的初始电平，仅输出模式有效 */
} iot_gpio_config_t;

/*
 * 初始化引脚。
 * pin    : 逻辑引脚编码。
 * config : 配置项，不可为 NULL。
 * 返回   : IOT_OK 成功；IOT_INVALID_PARAM；不支持的模式返回 IOT_NOT_SUPPORTED。
 */
iot_status_t iot_gpio_init(iot_pin_t pin, const iot_gpio_config_t *config);

/*
 * 复位引脚到默认状态（通常为浮空输入）。
 * pin  : 逻辑引脚编码。
 * 返回 : IOT_OK 成功；IOT_INVALID_PARAM。
 */
iot_status_t iot_gpio_deinit(iot_pin_t pin);

/*
 * 写引脚电平。
 * pin   : 逻辑引脚编码，需已配置为输出。
 * level : 目标电平。
 * 返回  : IOT_OK 成功；IOT_INVALID_PARAM。
 */
iot_status_t iot_gpio_write(iot_pin_t pin, iot_gpio_level_t level);

/*
 * 读引脚电平。
 * pin   : 逻辑引脚编码。
 * level : 输出参数，不可为 NULL。
 * 返回  : IOT_OK 成功；pin/level 非法返回 IOT_INVALID_PARAM。
 */
iot_status_t iot_gpio_read(iot_pin_t pin, iot_gpio_level_t *level);

/*
 * 翻转引脚电平。
 * pin  : 逻辑引脚编码，需已配置为输出。
 * 返回 : IOT_OK 成功；IOT_INVALID_PARAM。
 * 注意 : 实现上为读改写，非原子，多中断并发操作同一引脚需加保护。
 */
iot_status_t iot_gpio_toggle(iot_pin_t pin);

/* 置高电平，等价于 write(pin, IOT_GPIO_HIGH) */
iot_status_t iot_gpio_set(iot_pin_t pin);

/* 置低电平，等价于 write(pin, IOT_GPIO_LOW) */
iot_status_t iot_gpio_reset(iot_pin_t pin);

#ifdef __cplusplus
}
#endif

#endif /* IOT_GPIO_H */
