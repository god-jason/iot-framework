#ifndef DS2431_H
#define DS2431_H

/*
 * DS2431 1-Wire EEPROM 驱动（单总线 GPIO，开漏 + 上拉）。
 *
 * 一次写操作限制在一个 32 字节页内且不超过 8 字节 scratchpad，
 * 流程为：Write Scratchpad -> Read Scratchpad（含 CRC16 校验）-> Copy Scratchpad。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    iot_pin_t pin;
    uint8_t rom[8];
    bool rom_valid;
    bool initialized;
} ds2431_handle_t;

int ds2431_init(ds2431_handle_t *handle, iot_pin_t pin);
int ds2431_deinit(ds2431_handle_t *handle);

/* 读 8 字节 ROM（family=0x2D），含 CRC8 校验 */
int ds2431_read_rom(ds2431_handle_t *handle, uint8_t rom[8]);
/* 读存储区，任意长度（0..127） */
int ds2431_read(ds2431_handle_t *handle, uint8_t address, uint8_t *data, size_t len);
/* 写存储区：len <= 8 且不跨 32 字节页边界 */
int ds2431_write(ds2431_handle_t *handle, uint8_t address, const uint8_t *data, size_t len);
/* 总线复位，返回 1 表示检测到器件存在脉冲 */
int ds2431_reset(ds2431_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* DS2431_H */
