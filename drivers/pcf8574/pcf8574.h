#ifndef PCF8574_H
#define PCF8574_H

/*
 * PCF8574 / PCF8574A 8 位 I2C IO 扩展驱动。
 *
 * 器件为准双向口：写 1 后引脚由上拉保持高电平，可直接作为输入读取。
 * 参考 libdriver 风格的 pcf8574_init / pcf8574_write / pcf8574_read。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCF8574_DEFAULT_ADDRESS     0x20U  /* PCF8574: 0x20~0x27 */
#define PCF8574A_DEFAULT_ADDRESS    0x38U  /* PCF8574A: 0x38~0x3F */

typedef struct {
    driver_i2c_t i2c;
    uint8_t output_latch;   /* 影子寄存器，避免读改写丢失输出状态 */
    bool initialized;
} pcf8574_handle_t;

int pcf8574_init(pcf8574_handle_t *handle, hal_i2c_t *i2c, uint16_t address);
int pcf8574_deinit(pcf8574_handle_t *handle);

/** 写整个端口 */
int pcf8574_write(pcf8574_handle_t *handle, uint8_t value);

/** 读整个端口 */
int pcf8574_read(pcf8574_handle_t *handle, uint8_t *value);

/** 写单个引脚（pin: 0~7） */
int pcf8574_write_pin(pcf8574_handle_t *handle, uint8_t pin, bool level);

/** 读单个引脚（pin: 0~7） */
int pcf8574_read_pin(pcf8574_handle_t *handle, uint8_t pin, bool *level);

#ifdef __cplusplus
}
#endif

#endif /* PCF8574_H */
