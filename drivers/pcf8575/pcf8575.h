#ifndef PCF8575_H
#define PCF8575_H

/*
 * PCF8575 16 位 IO 扩展驱动（I2C，准双向口）。
 *
 * 参考 libdriver 风格：pcf8575_init / pcf8575_write / pcf8575_read。
 * 句柄内保存输出影子寄存器，避免读改写丢失输出状态。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    uint16_t output_latch;      /* 影子寄存器：最近一次写入的输出值 */
    bool initialized;
} pcf8575_handle_t;

/**
 * @brief  初始化：探测器件并把所有引脚置高（可作为输入）
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位地址（0x20~0x27），传 0 使用 0x20
 * @return 0 成功，负值为 hal_status_t
 */
int pcf8575_init(pcf8575_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

/** @brief 反初始化（不关闭总线） */
int pcf8575_deinit(pcf8575_handle_t *handle);

/** @brief 写整个 16 位端口（bit0 对应 P0.0） */
int pcf8575_write(pcf8575_handle_t *handle, uint16_t value);

/** @brief 读整个 16 位端口（bit0 对应 P0.0） */
int pcf8575_read(pcf8575_handle_t *handle, uint16_t *value);

/** @brief 写单个引脚（pin: 0~15） */
int pcf8575_write_pin(pcf8575_handle_t *handle, uint8_t pin, bool level);

/** @brief 读单个引脚（pin: 0~15） */
int pcf8575_read_pin(pcf8575_handle_t *handle, uint8_t pin, bool *level);

/** @brief 读取当前的输出影子寄存器 */
int pcf8575_get_latch(pcf8575_handle_t *handle, uint16_t *value);

#ifdef __cplusplus
}
#endif

#endif /* PCF8575_H */
