/*
 * Modbus RTU / TCP 公共帧工具。
 * 帧格式：
 *   RTU: [slave][PDU][crc16_le]
 *   TCP: [tid(2)][pid(2)=0][len(2)=unit+PDU][unit][PDU]（无 CRC）
 * 线圈位按字节打包（低 bit 在前），寄存器按大端 uint16。
 *
 * 本文件只负责“字节层”的组帧/解帧与请求构造，不涉及链路收发
 * （收发由主站 transport 回调或从站上层完成）。
 */
#include "modbus.h"
#include "crc.h"

#include <string.h>

/* 协议模块初始化：当前无全局状态，直接返回成功。 */
int modbus_init(void)
{
    return 0;
}

/* 计算 Modbus CRC16（委托给通用 CRC 模块）。 */
uint16_t modbus_crc16(const uint8_t *data, uint32_t len)
{
    return crc16_modbus(data, len);
}

/* ================= 基础编解码 ================= */

/* 大端写入 16 位（高字节在前）。 */
void modbus_put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

/* 大端读取 16 位（高字节在前）。 */
uint16_t modbus_get_be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

/* 位数量换算字节数：ceil(qty/8)，用 +7 再整除实现向上取整。 */
uint32_t modbus_bits_to_bytes(uint16_t qty)
{
    return ((uint32_t)qty + 7U) / 8U;
}

/* ================= RTU / TCP 成帧 ================= */

/*
 * 将 PDU 封装为完整 ADU。
 * 参数：mode 传输模式；slave 从站/单元地址；pdu/pdu_len 协议数据单元；
 *       out/capacity 输出缓冲及其容量。
 * 返回：ADU 总长度；0 表示容量不足。
 * 说明：TCP 模式下事务号固定填 1（主站发送前可覆盖为自增事务号），
 *       协议号固定 0，长度域 = PDU 长度 + 1（含单元标识）。
 *       RTU 模式下 CRC 低字节在前、高字节在后。
 */
uint32_t modbus_frame_adu(modbus_mode_t mode, uint8_t slave,
                          const uint8_t *pdu, uint32_t pdu_len,
                          uint8_t *out, uint32_t capacity)
{
    if (mode == MODBUS_TCP) {
        uint32_t total = MODBUS_TCP_MBAP_LEN + pdu_len;

        if (total > capacity) {
            return 0;
        }
        modbus_put_be16(&out[0], 0x0001);            /* transaction id */
        modbus_put_be16(&out[2], 0x0000);            /* protocol id */
        modbus_put_be16(&out[4], (uint16_t)(pdu_len + 1)); /* unit + PDU */
        out[6] = slave;                              /* unit id */
        memcpy(&out[7], pdu, pdu_len);
        return total;
    }

    /* RTU：1 字节从站地址 + PDU + 2 字节 CRC */
    {
        uint32_t total = 1 + pdu_len + 2;
        uint16_t crc;

        if (total > capacity) {
            return 0;
        }
        out[0] = slave;
        memcpy(&out[1], pdu, pdu_len);
        /* CRC 覆盖从站地址与 PDU，不含 CRC 自身 */
        crc = modbus_crc16(out, 1 + pdu_len);
        out[1 + pdu_len] = (uint8_t)(crc & 0xFF);
        out[2 + pdu_len] = (uint8_t)(crc >> 8);
        return total;
    }
}

/*
 * 校验并拆解 ADU，输出从站地址与 PDU 视图（指向入参缓冲区内部，不拷贝）。
 * 参数：mode 传输模式；slave 输出从站/单元地址；adu/len 输入帧；
 *       pdu/pdu_len 输出 PDU 指针与长度。
 * 返回：0 成功；-1 表示帧非法（长度不足、协议号非 0、长度域不符或
 *       CRC 校验失败）。
 */
int modbus_unframe_adu(modbus_mode_t mode, uint8_t *slave,
                       const uint8_t *adu, uint32_t len,
                       const uint8_t **pdu, uint32_t *pdu_len)
{
    if (mode == MODBUS_TCP) {
        uint16_t declared;

        if (adu == NULL || len < MODBUS_TCP_MBAP_LEN + 2) {
            return -1;
        }
        if (modbus_get_be16(&adu[2]) != 0x0000) {   /* protocol id 必须为 0 */
            return -1;
        }
        declared = modbus_get_be16(&adu[4]);
        /* 长度域 = unit(1) + PDU，故应有 len = MBAP(7) + 长度域 - 1 */
        if ((uint32_t)declared + MODBUS_TCP_MBAP_LEN - 1 != len) {
            return -1;               /* 长度不一致 */
        }
        *slave = adu[6];
        *pdu = &adu[7];
        *pdu_len = (uint32_t)declared - 1;
        return 0;
    }

    /* RTU：尾部 2 字节为小端 CRC，与计算值比较 */
    {
        uint16_t crc, calc;

        if (adu == NULL || len < 4) {
            return -1;
        }
        crc = (uint16_t)(adu[len - 1] << 8) | adu[len - 2];
        calc = modbus_crc16(adu, len - 2);
        if (crc != calc) {
            return -1;
        }
        *slave = adu[0];
        *pdu = &adu[1];
        *pdu_len = len - 3;
        return 0;
    }
}

/*
 * 按功能码构造请求 ADU（不含事务处理）。
 * 参数：mode RTU/TCP；slave 从站地址；fc 功能码；addr 起始地址；qty 数量；
 *       data/data_len 写入数据（读功能码可传 NULL/0）；out/capacity 输出缓冲；
 *       out_len 输出 ADU 长度。
 * 返回：0 成功；-1 参数非法（功能码不支持、数量超限、写入数据长度不符
 *       或输出容量不足）。
 * 数量上限遵循 Modbus 规范：读线圈/离散输入 2000，读寄存器 125，
 * 写多线圈 1968，写多寄存器 123。
 */
int modbus_build_request(modbus_mode_t mode, uint8_t slave, uint8_t fc,
                         uint16_t addr, uint16_t qty,
                         const uint8_t *data, uint16_t data_len,
                         uint8_t *out, uint32_t capacity, uint32_t *out_len)
{
    uint8_t pdu[MODBUS_TCP_MAX_ADU];
    uint32_t pdu_len = 0;
    uint32_t total;

    if (out == NULL || out_len == NULL) {
        return -1;
    }

    switch (fc) {
    /* 读类：PDU = [fc][addr(2)][qty(2)] */
    case MODBUS_FC_READ_COILS:
    case MODBUS_FC_READ_DISCRETE_INPUTS:
    case MODBUS_FC_READ_HOLDING_REGS:
    case MODBUS_FC_READ_INPUT_REGS:
        if (qty == 0 || qty > (fc <= 2 ? 2000 : 125)) {
            return -1;
        }
        pdu[0] = fc;
        modbus_put_be16(&pdu[1], addr);
        modbus_put_be16(&pdu[3], qty);
        pdu_len = 5;
        break;

    /* 写单线圈：0xFF00 表示 ON，0x0000 表示 OFF */
    case MODBUS_FC_WRITE_SINGLE_COIL:
        pdu[0] = fc;
        modbus_put_be16(&pdu[1], addr);
        modbus_put_be16(&pdu[3], (data_len > 0 && data[0]) ? 0xFF00 : 0x0000);
        pdu_len = 5;
        break;

    /* 写单寄存器：数据 2 字节大端，缺省补 0 */
    case MODBUS_FC_WRITE_SINGLE_REG:
        pdu[0] = fc;
        modbus_put_be16(&pdu[1], addr);
        if (data_len >= 2) {
            pdu[3] = data[0];
            pdu[4] = data[1];
        } else {
            pdu[3] = 0;
            pdu[4] = 0;
        }
        pdu_len = 5;
        break;

    /* 写多线圈：PDU = [fc][addr][qty][byte_count][打包位数据] */
    case MODBUS_FC_WRITE_MULTI_COILS:
        if (qty == 0 || qty > 1968 || data == NULL ||
            data_len != modbus_bits_to_bytes(qty)) {
            return -1;
        }
        pdu[0] = fc;
        modbus_put_be16(&pdu[1], addr);
        modbus_put_be16(&pdu[3], qty);
        pdu[5] = (uint8_t)data_len;
        memcpy(&pdu[6], data, data_len);
        pdu_len = 6 + data_len;
        break;

    /* 写多寄存器：PDU = [fc][addr][qty][byte_count][大端寄存器数据] */
    case MODBUS_FC_WRITE_MULTI_REGS:
        if (qty == 0 || qty > 123 || data == NULL ||
            data_len != qty * 2) {
            return -1;
        }
        pdu[0] = fc;
        modbus_put_be16(&pdu[1], addr);
        modbus_put_be16(&pdu[3], qty);
        pdu[5] = (uint8_t)data_len;
        memcpy(&pdu[6], data, data_len);
        pdu_len = 6 + data_len;
        break;

    default:
        return -1;
    }

    /* 统一加上 RTU 的地址/CRC 或 TCP 的 MBAP 头 */
    total = modbus_frame_adu(mode, slave, pdu, pdu_len, out, capacity);
    if (total == 0) {
        return -1;
    }
    *out_len = total;
    return 0;
}

/*
 * 解析响应 ADU。
 * 参数：mode RTU/TCP；adu/len 收到的完整帧；fc 输出功能码（已去除异常标志位，
 *       可为 NULL）；out_data/cap 输出 PDU 数据区及容量；out_data_len 输出
 *       实际拷贝的数据长度（可为 NULL）。
 * 返回：0 成功；-1 帧非法；正值表示从站异常码（MODBUS_EX_*）。
 * 说明：异常响应时功能码最高位为 1，此时返回紧随其后的异常码，不拷贝数据。
 *       正常响应时数据区即 PDU 第 1 字节起的全部内容（含字节数字段），
 *       拷贝长度取 min(pdu_len-1, cap)，由调用方按功能码自行解释。
 */
int modbus_parse_response(modbus_mode_t mode, const uint8_t *adu, uint32_t len,
                          uint8_t *fc, uint8_t *out_data, uint32_t cap,
                          uint32_t *out_data_len)
{
    uint8_t slave;
    const uint8_t *pdu;
    uint32_t pdu_len;
    int r;

    r = modbus_unframe_adu(mode, &slave, adu, len, &pdu, &pdu_len);
    if (r != 0 || pdu_len < 2) {
        return -1;
    }

    /* 功能码取低 7 位，便于调用方直接比对请求功能码 */
    if (fc != NULL) {
        *fc = pdu[0] & 0x7F;
    }

    /* 异常响应：fc 最高位置位 */
    if (pdu[0] & 0x80) {
        return (int)(pdu_len >= 2) ? pdu[1] : MODBUS_EX_DEVICE_FAILURE;
    }

    /* 正常响应：数据区为 PDU 去掉功能码后的内容，按容量截断 */
    if (out_data != NULL && out_data_len != NULL) {
        uint32_t n = pdu_len - 1;

        if (n > cap) {
            n = cap;
        }
        memcpy(out_data, &pdu[1], n);
        *out_data_len = n;
    }
    return 0;
}
