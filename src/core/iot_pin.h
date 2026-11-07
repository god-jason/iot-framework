#ifndef IOT_PIN_H
#define IOT_PIN_H

/*
 * 引脚复用配置。GPIO 电气属性由 iot_gpio 管理。
 *
 * 本模块只负责把逻辑引脚（iot_pin_t）设置为 GPIO / 复用功能 / 模拟模式，
 * 不处理上下拉、速度等电气参数；那些参数请通过 iot_gpio_init() 配置。
 * 逻辑引脚编码由本文件的 IOT_PIN() 宏定义。
 */

#include "iot_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 逻辑引脚编码 ----------------
 * 高 8 位为端口号，低 8 位为引脚号；具体端口号到硬件端口的映射由平台决定。
 */
typedef uint32_t iot_pin_t;

/* 由端口号与引脚号构造逻辑引脚编码 */
#define IOT_PIN(port, pin) \
    ((((uint32_t)(port)) << 8) | ((uint32_t)(pin) & 0xFFU))
#define IOT_PIN_GET_PORT(pin)  (((pin) >> 8) & 0xFFU)  /* 提取端口号 */
#define IOT_PIN_GET_INDEX(pin) ((pin) & 0xFFU)          /* 提取引脚号 */

/* 引脚功能选择 */
typedef enum {
    IOT_PIN_FUNCTION_GPIO = 0,  /* 普通 GPIO，由 iot_gpio 控制 */
    IOT_PIN_FUNCTION_AF,        /* 复用功能，需配合 alternate 编号 */
    IOT_PIN_FUNCTION_ANALOG     /* 模拟模式（ADC/DAC/COMP 等） */
} iot_pin_function_t;

/* 单个引脚的复用配置 */
typedef struct {
    iot_pin_t pin;              /* 逻辑引脚编码（IOT_PIN(port, pin)） */
    iot_pin_function_t function;/* 目标功能 */
    uint8_t alternate;           /* 复用功能编号，仅 IOT_PIN_FUNCTION_AF 时有效 */
} iot_pin_config_t;

/*
 * 初始化引脚复用。
 * config : 配置项，不可为 NULL；function 为 AF 时必须填写合法 alternate。
 * 返回   : IOT_OK 成功；config 为 NULL 返回 IOT_INVALID_PARAM；
 *          平台不支持该复用编号时返回 IOT_NOT_SUPPORTED。
 */
iot_status_t iot_pin_init(const iot_pin_config_t *config);

/*
 * 复位引脚复用为默认状态（通常恢复为模拟/浮空输入）。
 * pin  : 逻辑引脚编码。
 * 返回 : IOT_OK 成功；引脚非法返回 IOT_INVALID_PARAM。
 */
iot_status_t iot_pin_deinit(iot_pin_t pin);

#ifdef __cplusplus
}
#endif

#endif /* IOT_PIN_H */
