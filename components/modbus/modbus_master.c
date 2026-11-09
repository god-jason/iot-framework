/*
 * Modbus 主站：请求组帧、transport 收发、响应解析与寄存器读写。
 *
 * 数据流：读写 API -> master_transact（组帧/收发/解帧/功能码比对）->
 *         解析数据区 -> 写回调用方缓冲。
 * 所有读写接口返回 0 成功、负值本地错误（MODBUS_ERR_*）、正值从站异常码。
 */
#include "modbus.h"

#include <string.h>

/*
 * 初始化主站并清零统计。
 * 参数：master 主站对象；mode RTU/TCP；address 默认从站地址；transport 收发
 *       回调（可稍后设置）；user 回调上下文；timeout_ms 单帧超时。
 * 返回：0 成功；MODBUS_ERR_PARAM 表示 master 为空。
 */
int modbus_master_init(modbus_master_t *master, modbus_mode_e mode, uint8_t address,
                       modbus_transport_fn transport, void *user, uint32_t timeout_ms)
{
    if (master == NULL) {
        return MODBUS_ERR_PARAM;
    }
    memset(master, 0, sizeof(*master));
    master->mode = mode;
    master->address = address;
    master->transport = transport;
    master->user = user;
    master->timeout_ms = timeout_ms;
    master->transaction_id = 0;
    return 0;
}

/* 替换收发回调与上下文；master 为空时静默返回。 */
void modbus_master_set_transport(modbus_master_t *master, modbus_transport_fn transport,
                                 void *user)
{
    if (master == NULL) {
        return;
    }
    master->transport = transport;
    master->user = user;
}

/* 修改从站地址；master 为空时静默返回。 */
void modbus_master_set_address(modbus_master_t *master, uint8_t address)
{
    if (master != NULL) {
        master->address = address;
    }
}

/* 一次完整事务：组帧 -> transport -> 解帧 -> 校验功能码。
 * resp 返回 PDU 数据区（含读响应中的字节数字）。 */

/*
 * 执行一次 Modbus 事务。
 * 参数：master 主站；fc 功能码；addr 起始地址；qty 数量；wdata/wlen 写入数据
 *       （读操作传 NULL/0）；resp/resp_cap 响应数据区及容量；resp_len 输出
 *       数据区长度（可为 NULL）。
 * 返回：0 成功；MODBUS_ERR_PARAM 参数/组帧错误；MODBUS_ERR_TRANSPORT
 *       链路失败；MODBUS_ERR_FRAME 帧非法或响应功能码不匹配；正值从站异常码。
 * 说明：TCP 模式下每次事务递增 transaction_id 并回填到 MBAP 头前两字节；
 *       无论成败都会更新 request_count / error_count。
 */
static int master_transact(modbus_master_t *master, uint8_t fc,
                           uint16_t addr, uint16_t qty,
                           const uint8_t *wdata, uint16_t wlen,
                           uint8_t *resp, uint32_t resp_cap, uint32_t *resp_len)
{
    uint8_t req[MODBUS_TCP_MAX_ADU];
    uint8_t rsp[MODBUS_TCP_MAX_ADU];
    uint32_t req_len = 0;
    uint32_t rsp_len = 0;
    uint32_t data_len = 0;
    uint8_t rfc = 0;
    int r;

    if (master == NULL || master->transport == NULL || resp == NULL) {
        return MODBUS_ERR_PARAM;
    }
    /* 1) 组帧：写入 RTU 地址/CRC 或 TCP MBAP 头 */
    if (modbus_build_request(master->mode, master->address, fc, addr, qty,
                             wdata, wlen, req, sizeof(req), &req_len) != 0) {
        return MODBUS_ERR_PARAM;
    }
    /* 2) TCP 事务号自增并回填 MBAP 前两字节（大端） */
    if (master->mode == MODBUS_TCP) {
        master->transaction_id++;
        req[0] = (uint8_t)(master->transaction_id >> 8);
        req[1] = (uint8_t)(master->transaction_id & 0xFF);
    }

    master->request_count++;
    /* 3) 交给 transport 完成一帧收发 */
    r = master->transport(master->user, req, req_len, rsp, sizeof(rsp),
                          &rsp_len, master->timeout_ms);
    if (r != 0 || rsp_len == 0) {
        master->error_count++;
        return MODBUS_ERR_TRANSPORT;
    }

    /* 4) 解析响应：CRC/MBAP 校验 + 异常码识别 */
    r = modbus_parse_response(master->mode, rsp, rsp_len, &rfc,
                              resp, resp_cap, &data_len);
    if (r > 0) {
        master->error_count++;
        return r;                       /* Modbus 异常码 */
    }
    if (r != 0) {
        master->error_count++;
        return MODBUS_ERR_FRAME;
    }
    /* 5) 响应功能码必须与请求一致，否则视为错帧 */
    if (rfc != fc) {
        master->error_count++;
        return MODBUS_ERR_FRAME;
    }
    if (resp_len != NULL) {
        *resp_len = data_len;
    }
    return 0;
}

/*
 * 读线圈（FC01）。
 * 参数：master 主站；addr 起始地址；qty 数量（1..2000）；bits 输出打包位缓冲。
 * 返回：0 成功；负值本地错误；正值从站异常码。
 * 说明：响应数据区首字节为字节数，必须等于 ceil(qty/8)。
 */
int modbus_master_read_coils(modbus_master_t *master, uint16_t addr, uint16_t qty,
                             uint8_t *bits)
{
    uint8_t buf[MODBUS_TCP_MAX_ADU];
    uint32_t len = 0;
    int r;

    if (bits == NULL || qty == 0 || qty > 2000) {
        return MODBUS_ERR_PARAM;
    }
    r = master_transact(master, MODBUS_FC_READ_COILS, addr, qty, NULL, 0,
                        buf, sizeof(buf), &len);
    if (r != 0) {
        return r;
    }
    /* 字节数字段与实际数据长度双重校验 */
    if (len < 1 || buf[0] != (uint8_t)modbus_bits_to_bytes(qty) ||
        len < 1u + buf[0]) {
        return MODBUS_ERR_FRAME;
    }
    memcpy(bits, &buf[1], buf[0]);
    return 0;
}

/*
 * 读离散输入（FC02）。
 * 参数：master 主站；addr 起始地址；qty 数量（1..2000）；bits 输出打包位缓冲。
 * 返回：0 成功；负值本地错误；正值从站异常码。
 */
int modbus_master_read_discrete(modbus_master_t *master, uint16_t addr, uint16_t qty,
                                uint8_t *bits)
{
    uint8_t buf[MODBUS_TCP_MAX_ADU];
    uint32_t len = 0;
    int r;

    if (bits == NULL || qty == 0 || qty > 2000) {
        return MODBUS_ERR_PARAM;
    }
    r = master_transact(master, MODBUS_FC_READ_DISCRETE_INPUTS, addr, qty,
                        NULL, 0, buf, sizeof(buf), &len);
    if (r != 0) {
        return r;
    }
    if (len < 1 || buf[0] != (uint8_t)modbus_bits_to_bytes(qty) ||
        len < 1u + buf[0]) {
        return MODBUS_ERR_FRAME;
    }
    memcpy(bits, &buf[1], buf[0]);
    return 0;
}

/*
 * 读寄存器通用实现（FC03 / FC04）。
 * 参数：master 主站；fc 功能码；addr 起始地址；qty 数量（1..125）；regs 输出数组。
 * 返回：0 成功；负值本地错误；正值从站异常码。
 * 说明：响应字节数字段应为 qty*2，数据按大端逐字解析。
 */
static int master_read_regs(modbus_master_t *master, uint8_t fc,
                            uint16_t addr, uint16_t qty, uint16_t *regs)
{
    uint8_t buf[MODBUS_TCP_MAX_ADU];
    uint32_t len = 0;
    uint16_t i;
    int r;

    if (regs == NULL || qty == 0 || qty > 125) {
        return MODBUS_ERR_PARAM;
    }
    r = master_transact(master, fc, addr, qty, NULL, 0, buf, sizeof(buf), &len);
    if (r != 0) {
        return r;
    }
    /* 校验字节数（2*qty）与已接收长度 */
    if (len < 1 || buf[0] != (uint8_t)(qty * 2) ||
        len < 1u + (uint32_t)qty * 2u) {
        return MODBUS_ERR_FRAME;
    }
    for (i = 0; i < qty; i++) {
        regs[i] = modbus_get_be16(&buf[1 + (uint32_t)i * 2]);
    }
    return 0;
}

/* 读保持寄存器（FC03），详见 master_read_regs。 */
int modbus_master_read_holding(modbus_master_t *master, uint16_t addr, uint16_t qty,
                               uint16_t *regs)
{
    return master_read_regs(master, MODBUS_FC_READ_HOLDING_REGS, addr, qty, regs);
}

/* 读输入寄存器（FC04），详见 master_read_regs。 */
int modbus_master_read_input(modbus_master_t *master, uint16_t addr, uint16_t qty,
                             uint16_t *regs)
{
    return master_read_regs(master, MODBUS_FC_READ_INPUT_REGS, addr, qty, regs);
}

/*
 * 写单个线圈（FC05）。
 * 参数：master 主站；addr 地址；on 非 0 写 ON（0xFF00），0 写 OFF（0x0000）。
 * 返回：0 成功；负值本地错误；正值从站异常码。
 * 说明：正常响应为请求回显，需校验地址与输出值完全一致。
 */
int modbus_master_write_coil(modbus_master_t *master, uint16_t addr, uint8_t on)
{
    uint8_t wdata[1];
    uint8_t buf[MODBUS_TCP_MAX_ADU];
    uint32_t len = 0;
    int r;

    wdata[0] = on ? 1U : 0U;
    r = master_transact(master, MODBUS_FC_WRITE_SINGLE_COIL, addr, 1, wdata, 1,
                        buf, sizeof(buf), &len);
    if (r != 0) {
        return r;
    }
    if (len != 4 || modbus_get_be16(buf) != addr ||
        modbus_get_be16(&buf[2]) != (on ? 0xFF00u : 0x0000u)) {
        return MODBUS_ERR_FRAME;
    }
    return 0;
}

/*
 * 写单个保持寄存器（FC06）。
 * 参数：master 主站；addr 地址；value 写入值。
 * 返回：0 成功；负值本地错误；正值从站异常码。
 * 说明：响应需回显地址与写入值。
 */
int modbus_master_write_reg(modbus_master_t *master, uint16_t addr, uint16_t value)
{
    uint8_t wdata[2];
    uint8_t buf[MODBUS_TCP_MAX_ADU];
    uint32_t len = 0;
    int r;

    wdata[0] = (uint8_t)(value >> 8);
    wdata[1] = (uint8_t)(value & 0xFF);
    r = master_transact(master, MODBUS_FC_WRITE_SINGLE_REG, addr, 1, wdata, 2,
                        buf, sizeof(buf), &len);
    if (r != 0) {
        return r;
    }
    if (len != 4 || modbus_get_be16(buf) != addr ||
        modbus_get_be16(&buf[2]) != value) {
        return MODBUS_ERR_FRAME;
    }
    return 0;
}

/*
 * 写多个线圈（FC0F）。
 * 参数：master 主站；addr 起始地址；qty 数量（1..1968）；bits 打包位数据。
 * 返回：0 成功；负值本地错误；正值从站异常码。
 * 说明：正常响应只返回起始地址与数量，需回显校验。
 */
int modbus_master_write_coils(modbus_master_t *master, uint16_t addr, uint16_t qty,
                              const uint8_t *bits)
{
    uint8_t buf[MODBUS_TCP_MAX_ADU];
    uint32_t len = 0;
    uint16_t bytes;
    int r;

    if (bits == NULL || qty == 0 || qty > 1968) {
        return MODBUS_ERR_PARAM;
    }
    bytes = (uint16_t)modbus_bits_to_bytes(qty);
    r = master_transact(master, MODBUS_FC_WRITE_MULTI_COILS, addr, qty,
                        bits, bytes, buf, sizeof(buf), &len);
    if (r != 0) {
        return r;
    }
    if (len != 4 || modbus_get_be16(buf) != addr ||
        modbus_get_be16(&buf[2]) != qty) {
        return MODBUS_ERR_FRAME;
    }
    return 0;
}

/*
 * 写多个保持寄存器（FC10）。
 * 参数：master 主站；addr 起始地址；qty 数量（1..123）；regs 寄存器数组。
 * 返回：0 成功；负值本地错误；正值从站异常码。
 * 说明：最多 123 个寄存器（246 字节），本地先转成大端字节流再发送。
 */
int modbus_master_write_regs(modbus_master_t *master, uint16_t addr, uint16_t qty,
                             const uint16_t *regs)
{
    uint8_t wdata[246];
    uint8_t buf[MODBUS_TCP_MAX_ADU];
    uint32_t len = 0;
    uint16_t i;
    int r;

    if (regs == NULL || qty == 0 || qty > 123) {
        return MODBUS_ERR_PARAM;
    }
    /* 主机侧统一按大端打包，接收端再按大端还原 */
    for (i = 0; i < qty; i++) {
        modbus_put_be16(&wdata[(uint32_t)i * 2], regs[i]);
    }
    r = master_transact(master, MODBUS_FC_WRITE_MULTI_REGS, addr, qty,
                        wdata, (uint16_t)(qty * 2), buf, sizeof(buf), &len);
    if (r != 0) {
        return r;
    }
    if (len != 4 || modbus_get_be16(buf) != addr ||
        modbus_get_be16(&buf[2]) != qty) {
        return MODBUS_ERR_FRAME;
    }
    return 0;
}
