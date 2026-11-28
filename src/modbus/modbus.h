#ifndef MODBUS_H
#define MODBUS_H

/*
 * Modbus 协议公共部分（RTU / TCP）。
 *
 * 帧格式：
 *   RTU: [slave][PDU][crc16_le]
 *   TCP: [tid(2)][pid(2)=0][len(2)=unit+PDU][unit][PDU]（无 CRC）
 *
 * 本文件提供协议常量、CRC、请求组帧与响应解析；
 * 主站见 modbus_master.h，从站见 modbus_slave.h。
 * 为便于使用，本头文件末尾会同时包含主站与从站接口。
 *
 * 字节序：所有多字节字段均为大端（网络序）；RTU 的 CRC 低字节在前。
 * 线圈：按字节打包，字节内低位对应较小地址。
 * 返回约定：0 成功；负值见 MODBUS_ERR_*；大于 0 表示对端返回的异常码。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 传输模式：RTU（串行总线）或 TCP（MBAP 封装） */
typedef enum {
    MODBUS_RTU,             /* RTU：从站地址 + PDU + CRC16 */
    MODBUS_TCP              /* TCP：MBAP 头 + 单元标识 + PDU */
} modbus_mode_e;

/* 功能码 */
#define MODBUS_FC_READ_COILS           0x01  /* 读线圈 */
#define MODBUS_FC_READ_DISCRETE_INPUTS 0x02  /* 读离散输入 */
#define MODBUS_FC_READ_HOLDING_REGS    0x03  /* 读保持寄存器 */
#define MODBUS_FC_READ_INPUT_REGS      0x04  /* 读输入寄存器 */
#define MODBUS_FC_WRITE_SINGLE_COIL    0x05  /* 写单个线圈 */
#define MODBUS_FC_WRITE_SINGLE_REG     0x06  /* 写单个寄存器 */
#define MODBUS_FC_WRITE_MULTI_COILS    0x0F  /* 写多个线圈 */
#define MODBUS_FC_WRITE_MULTI_REGS     0x10  /* 写多个寄存器 */

/* 异常码（响应功能码最高位置 1 后跟随的 1 字节） */
#define MODBUS_EX_ILLEGAL_FUNCTION     0x01  /* 不支持的功能码 */
#define MODBUS_EX_ILLEGAL_DATA_ADDR    0x02  /* 地址越界/未映射 */
#define MODBUS_EX_ILLEGAL_DATA_VALUE   0x03  /* 数量或取值非法 */
#define MODBUS_EX_DEVICE_FAILURE       0x04  /* 从站内部执行失败 */

/* 返回值（负值）；返回值 > 0 时表示 Modbus 异常码 */
#define MODBUS_ERR_PARAM               (-1)  /* 参数非法 */
#define MODBUS_ERR_TRANSPORT           (-2)  /* 链路收发失败/超时 */
#define MODBUS_ERR_FRAME               (-3)  /* 帧格式或校验错误 */
#define MODBUS_ERR_RANGE               (-4)  /* 地址/区间不匹配 */
#define MODBUS_ERR_NO_MEMORY           (-5)  /* 映射表已满 */

/* TCP MBAP 头 7 字节，其中长度域（大端）为后续字节数（unit + PDU） */
#define MODBUS_TCP_MBAP_LEN            7
#define MODBUS_TCP_MAX_ADU             260

/* 初始化协议模块（当前无全局资源，仅返回 0）。 */
int modbus_init(void);

/*
 * 计算 Modbus CRC16（多项式 0xA001 反射算法，初值 0xFFFF）。
 * 参数：data 数据首地址；len 字节数。
 * 返回：CRC16 值。
 */
uint16_t modbus_crc16(const uint8_t *data, uint32_t len);

/* 构建请求 ADU（RTU：slave+PDU+CRC；TCP：MBAP+PDU）。
 * data/data_len 仅对写功能码有意义（线圈按位打包 / 寄存器按大端 uint16）。
 * 返回 0 成功，out_len 为完整 ADU 长度。 */
int modbus_build_request(modbus_mode_e mode, uint8_t slave, uint8_t fc,
                         uint16_t addr, uint16_t qty,
                         const uint8_t *data, uint16_t data_len,
                         uint8_t *out, uint32_t capacity, uint32_t *out_len);

/* 解析响应 ADU：校验帧（CRC/MBAP），填充 fc 与数据区。
 * 返回 0 成功；负值表示帧错误；正值表示异常码。 */
int modbus_parse_response(modbus_mode_e mode, const uint8_t *adu, uint32_t len,
                          uint8_t *fc, uint8_t *out_data, uint32_t cap,
                          uint32_t *out_data_len);

/* ================= 内部帧工具（主站/从站实现共用） ================= */

/* 以大端写入 16 位值到 p[0..1] */
void modbus_put_be16(uint8_t *p, uint16_t v);

/* 从 p[0..1] 以大端读出 16 位值 */
uint16_t modbus_get_be16(const uint8_t *p);

/* 计算 qty 个线圈/离散输入打包所需的字节数，即 ceil(qty/8) */
uint32_t modbus_bits_to_bytes(uint16_t qty);

/* 把 PDU 封装为 ADU；返回 ADU 长度，0 表示容量不足 */
uint32_t modbus_frame_adu(modbus_mode_e mode, uint8_t slave,
                          const uint8_t *pdu, uint32_t pdu_len,
                          uint8_t *out, uint32_t capacity);

/* 解帧并抽出 PDU；返回负值表示非法帧 */
int modbus_unframe_adu(modbus_mode_e mode, uint8_t *slave,
                       const uint8_t *adu, uint32_t len,
                       const uint8_t **pdu, uint32_t *pdu_len);

#ifdef __cplusplus
}
#endif

#include "modbus_master.h"
#include "modbus_slave.h"

#endif /* MODBUS_H */
