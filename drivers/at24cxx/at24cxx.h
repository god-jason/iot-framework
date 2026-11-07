#ifndef AT24CXX_H
#define AT24CXX_H

/*
 * AT24Cxx 系列 I2C EEPROM 驱动。
 *
 * 覆盖 AT24C01 ~ AT24C512，使用 16 位内存地址与分页写入。
 * 参考 libdriver 风格：at24cxx_init / at24cxx_read / at24cxx_write。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AT24CXX_DEFAULT_ADDRESS     0x50U

typedef enum {
    AT24CXX_MODEL_01 = 0,   /* 1 Kbit  = 128 B,  page 8   */
    AT24CXX_MODEL_02,       /* 2 Kbit  = 256 B,  page 8   */
    AT24CXX_MODEL_04,       /* 4 Kbit  = 512 B,  page 16  */
    AT24CXX_MODEL_08,       /* 8 Kbit  = 1 KB,   page 16  */
    AT24CXX_MODEL_16,       /* 16 Kbit = 2 KB,   page 16  */
    AT24CXX_MODEL_32,       /* 32 Kbit = 4 KB,   page 32  */
    AT24CXX_MODEL_64,       /* 64 Kbit = 8 KB,   page 32  */
    AT24CXX_MODEL_128,      /* 128 Kbit= 16 KB,  page 64  */
    AT24CXX_MODEL_256,      /* 256 Kbit= 32 KB,  page 64  */
    AT24CXX_MODEL_512       /* 512 Kbit= 64 KB,  page 128 */
} at24cxx_model_t;

typedef struct {
    driver_i2c_t i2c;
    at24cxx_model_t model;
    uint32_t capacity;
    uint16_t page_size;
    uint16_t write_delay_ms;
    bool initialized;
} at24cxx_handle_t;

int at24cxx_init(at24cxx_handle_t *handle, iot_i2c_t *i2c,
                 uint16_t address, at24cxx_model_t model);
int at24cxx_deinit(at24cxx_handle_t *handle);

/** 从指定地址读取 */
int at24cxx_read(at24cxx_handle_t *handle, uint32_t address, uint8_t *data, size_t len);

/** 写入任意长度（自动按页拆分并等待写周期） */
int at24cxx_write(at24cxx_handle_t *handle, uint32_t address,
                  const uint8_t *data, size_t len);

/** 轮询 ACK 判断写周期是否结束 */
int at24cxx_is_ready(at24cxx_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* AT24CXX_H */
