#ifndef IOT_MODBUS_H
#define IOT_MODBUS_H

/*
 * Modbus RTU（Master）编解码。寄存器地址/数值按大端，帧尾 CRC16/MODBUS（低字节在前）。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IOT_MODBUS_RTU,
    IOT_MODBUS_TCP
} iot_modbus_mode_t;

typedef struct {
    uint8_t slave_id;
    uint8_t function;

    uint16_t address;
    uint16_t quantity;

    uint16_t *registers;
    uint16_t register_count;
} iot_modbus_request_t;

int iot_modbus_init(void);

uint16_t iot_modbus_crc16(const uint8_t *data, uint32_t len);

/* 编码：读保持寄存器（FC03），返回帧长 */
int iot_modbus_build_read_regs(uint8_t slave, uint16_t addr, uint16_t qty,
                               uint8_t *out, uint32_t capacity, uint32_t *len);

/* 编码：写单个寄存器（FC06） */
int iot_modbus_build_write_reg(uint8_t slave, uint16_t addr, uint16_t value,
                               uint8_t *out, uint32_t capacity, uint32_t *len);

/* 编码：写多个寄存器（FC10） */
int iot_modbus_build_write_regs(uint8_t slave, uint16_t addr,
                                const uint16_t *regs, uint16_t count,
                                uint8_t *out, uint32_t capacity, uint32_t *len);

/* 解码响应帧：校验 CRC，提取 slave/function 与数据区 */
int iot_modbus_decode(const uint8_t *frame, uint32_t len,
                      iot_modbus_request_t *req, const uint8_t **data, uint32_t *data_len);

#ifdef __cplusplus
}
#endif

#endif /* IOT_MODBUS_H */