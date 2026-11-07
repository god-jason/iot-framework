#ifndef MODBUS_H
#define MODBUS_H

/*
 * Modbus 协议（RTU / TCP）：完整主站与从站。
 * 寄存器/线圈地址与数值均按大端，RTU 帧尾 CRC16/MODBUS（低字节在前），
 * TCP 使用 MBAP 头（长度域为字节数，大端）。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MODBUS_RTU,
    MODBUS_TCP
} modbus_mode_t;

/* 功能码 */
#define MODBUS_FC_READ_COILS           0x01
#define MODBUS_FC_READ_DISCRETE_INPUTS 0x02
#define MODBUS_FC_READ_HOLDING_REGS    0x03
#define MODBUS_FC_READ_INPUT_REGS      0x04
#define MODBUS_FC_WRITE_SINGLE_COIL    0x05
#define MODBUS_FC_WRITE_SINGLE_REG     0x06
#define MODBUS_FC_WRITE_MULTI_COILS    0x0F
#define MODBUS_FC_WRITE_MULTI_REGS     0x10

/* 异常码 */
#define MODBUS_EX_ILLEGAL_FUNCTION     0x01
#define MODBUS_EX_ILLEGAL_DATA_ADDR    0x02
#define MODBUS_EX_ILLEGAL_DATA_VALUE   0x03
#define MODBUS_EX_DEVICE_FAILURE       0x04

/* TCP MBAP 头 7 字节，其中长度域（大端）为后续字节数（unit + PDU） */
#define MODBUS_TCP_MBAP_LEN            7
#define MODBUS_TCP_MAX_ADU             260

int modbus_init(void);

uint16_t modbus_crc16(const uint8_t *data, uint32_t len);

/* ================= 主站 =================
 * 构建请求 ADU（RTU：slave+PDU+CRC；TCP：MBAP+PDU）。
 * data/data_len 仅对写功能码有意义（线圈按位打包 / 寄存器按大端 uint16）。
 * 返回 0 成功，out_len 为完整 ADU 长度。
 */
int modbus_build_request(modbus_mode_t mode, uint8_t slave, uint8_t fc,
                         uint16_t addr, uint16_t qty,
                         const uint8_t *data, uint16_t data_len,
                         uint8_t *out, uint32_t capacity, uint32_t *out_len);

/* 解析主站收到的响应 ADU：校验帧（CRC/MBAP），填充 fc 与数据区。
 * out_data 缓冲由调用者提供，out_data_len 为数据字节数（PDU 数据区）。
 * 返回 0 成功；返回负值表示帧错误，返回正值为异常码（异常响应）。 */
int modbus_parse_response(modbus_mode_t mode, const uint8_t *adu, uint32_t len,
                          uint8_t *fc, uint8_t *out_data, uint32_t cap,
                          uint32_t *out_data_len);

/* ================= 从站 ================= */
/* 从站数据访问接口：由应用实现，绑定具体线圈/寄存器映射 */
typedef int (*modbus_read_bits_fn)(uint16_t addr, uint16_t qty, uint8_t *bits, void *user);
typedef int (*modbus_read_regs_fn)(uint16_t addr, uint16_t qty, uint16_t *regs, void *user);
typedef int (*modbus_write_bits_fn)(uint16_t addr, const uint8_t *bits, uint16_t qty, void *user);
typedef int (*modbus_write_regs_fn)(uint16_t addr, const uint16_t *regs, uint16_t qty, void *user);

typedef struct {
    modbus_read_bits_fn  read_coils;      /* FC01 */
    modbus_read_bits_fn  read_discrete;   /* FC02 */
    modbus_read_regs_fn  read_holding;    /* FC03 */
    modbus_read_regs_fn  read_input;      /* FC04 */
    modbus_write_bits_fn write_coils;     /* FC05 / FC0F */
    modbus_write_regs_fn write_regs;      /* FC06 / FC10 */
    void *user;
} modbus_slave_if_t;

/* 处理一条请求 ADU 并生成响应 ADU（含异常响应）。
 * 返回 0 成功（out_len 为响应长度）；返回负值表示输入帧非法（不应回复）。 */
int modbus_slave_process(modbus_mode_t mode, uint8_t slave,
                         const modbus_slave_if_t *iface,
                         const uint8_t *adu, uint32_t len,
                         uint8_t *out, uint32_t capacity, uint32_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_H */