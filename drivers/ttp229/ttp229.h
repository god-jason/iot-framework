#ifndef TTP229_H
#define TTP229_H

/*
 * TTP229 16 键电容触摸驱动。
 *
 * 同一驱动支持两种总线：
 *   - I2C（TTP229-BSF）：ttp229_init_i2c + ttp229_read_keys
 *   - 2 线 GPIO（TTP229-LSF/BSW）：ttp229_init_gpio + ttp229_read_keys
 *
 * 键位图：bit0 对应 K1，bit15 对应 K16。
 * 参考 libdriver 风格：ttp229_init / ttp229_read / ttp229_deinit。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 总线类型 */
typedef enum {
    TTP229_BUS_I2C = 0,
    TTP229_BUS_GPIO_2WIRE
} ttp229_bus_t;

typedef struct {
    ttp229_bus_t bus;
    driver_i2c_t i2c;       /* I2C 模式使用 */
    iot_pin_t scl;         /* 2 线模式：时钟输出 */
    iot_pin_t sdo;         /* 2 线模式：数据输入 */
    bool initialized;
} ttp229_handle_t;

/**
 * @brief  以 I2C 方式初始化（TTP229-BSF）
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位地址，传 0 使用 0x57
 * @return 0 成功，负值为 iot_status_t
 */
int ttp229_init_i2c(ttp229_handle_t *handle, iot_i2c_t *i2c, uint16_t address);

/**
 * @brief  以 2 线 GPIO 方式初始化（TTP229-LSF/BSW）
 * @param  handle  驱动句柄
 * @param  scl     SCL 引脚（主机输出）
 * @param  sdo     SDO 引脚（芯片输出，需上拉）
 * @return 0 成功，负值为 iot_status_t
 */
int ttp229_init_gpio(ttp229_handle_t *handle, iot_pin_t scl, iot_pin_t sdo);

/** @brief 反初始化 */
int ttp229_deinit(ttp229_handle_t *handle);

/**
 * @brief  读取 16 键位图
 * @param  keys  bit0 对应 K1 ... bit15 对应 K16
 * @return 0 成功，负值为 iot_status_t
 */
int ttp229_read_keys(ttp229_handle_t *handle, uint16_t *keys);

/**
 * @brief  读取当前按下的第一个键
 * @param  key  1~16，0 表示无按键
 */
int ttp229_read_key(ttp229_handle_t *handle, uint8_t *key);

#ifdef __cplusplus
}
#endif

#endif /* TTP229_H */
