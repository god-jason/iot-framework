#ifndef MODBUS_MASTER_H
#define MODBUS_MASTER_H

/*
 * Modbus 主站：通过 transport 回调完成一帧收发，
 * 内置请求组帧、响应解析、异常识别与统计。
 *
 * 一次事务流程：build_request -> transport(tx/rx) -> parse_response ->
 *              校验响应功能码 -> 提取数据区（见 modbus_master.c）。
 * 说明：本模块不感知串口/网络细节，RTU 与 TCP 仅由 mode 区分。
 */

#include "modbus.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 传输回调：发送 tx 并等待一帧响应写入 rx。
 * 参数：ctx 用户上下文；tx/tx_len 待发送 ADU；rx/rx_capacity 接收缓冲及容量；
 *       rx_len 输出实际接收长度；timeout_ms 等待超时（毫秒）。
 * 返回：0 表示成功；非 0 表示链路错误（超时、校验失败等），主站将计为传输错误。
 */
typedef int (*modbus_transport_fn)(void *ctx, const uint8_t *tx, uint32_t tx_len,
                                   uint8_t *rx, uint32_t rx_capacity,
                                   uint32_t *rx_len, uint32_t timeout_ms);

typedef struct {
    modbus_mode_t mode;         /* 传输模式：RTU 或 TCP */
    uint8_t address;            /* 从站地址 */
    uint16_t transaction_id;    /* TCP 事务号，自动递增 */
    uint32_t timeout_ms;        /* 单帧响应超时（毫秒） */

    modbus_transport_fn transport; /* 收发回调，由应用提供 */
    void *user;                 /* 透传给 transport 的上下文 */

    /* 统计 */
    uint32_t request_count;     /* 已发起的请求总数 */
    uint32_t error_count;       /* 链路/帧/异常响应累计错误数 */
} modbus_master_t;

/*
 * 初始化主站。
 * 参数：master 主站对象；mode RTU/TCP；address 默认从站地址；transport 收发回调
 *       （可传 NULL，稍后设置）；user 回调上下文；timeout_ms 超时时间。
 * 返回：0 成功；MODBUS_ERR_PARAM 表示 master 为空。
 */
int modbus_master_init(modbus_master_t *master, modbus_mode_t mode, uint8_t address,
                       modbus_transport_fn transport, void *user, uint32_t timeout_ms);

/* 运行时替换收发回调及其上下文（常用于链路切换）。 */
void modbus_master_set_transport(modbus_master_t *master, modbus_transport_fn transport,
                                 void *user);

/* 运行时修改从站地址。 */
void modbus_master_set_address(modbus_master_t *master, uint8_t address);

/* 读：bits 为按位打包缓冲（长度 >= (qty+7)/8）；regs 为 qty 个 uint16。
 * 返回 0 成功；负值见 MODBUS_ERR_*；正值表示 Modbus 异常码。 */

/*
 * 读线圈（FC01）。
 * 参数：master 主站；addr 起始地址；qty 数量（1..2000）；bits 输出位缓冲。
 * 返回：0 成功；负值为本地错误；正值为从站异常码。
 */
int modbus_master_read_coils(modbus_master_t *master, uint16_t addr, uint16_t qty,
                             uint8_t *bits);

/*
 * 读离散输入（FC02）。
 * 参数：master 主站；addr 起始地址；qty 数量（1..2000）；bits 输出位缓冲。
 * 返回：0 成功；负值为本地错误；正值为从站异常码。
 */
int modbus_master_read_discrete(modbus_master_t *master, uint16_t addr, uint16_t qty,
                                uint8_t *bits);

/*
 * 读保持寄存器（FC03）。
 * 参数：master 主站；addr 起始地址；qty 数量（1..125）；regs 输出数组。
 * 返回：0 成功；负值为本地错误；正值为从站异常码。
 */
int modbus_master_read_holding(modbus_master_t *master, uint16_t addr, uint16_t qty,
                               uint16_t *regs);

/*
 * 读输入寄存器（FC04）。
 * 参数：master 主站；addr 起始地址；qty 数量（1..125）；regs 输出数组。
 * 返回：0 成功；负值为本地错误；正值为从站异常码。
 */
int modbus_master_read_input(modbus_master_t *master, uint16_t addr, uint16_t qty,
                             uint16_t *regs);

/* 写：单点 / 多点（bits 为按位打包缓冲） */

/*
 * 写单个线圈（FC05）。
 * 参数：master 主站；addr 线圈地址；on 非 0 写 ON（0xFF00），0 写 OFF。
 * 返回：0 成功；负值为本地错误；正值为从站异常码。
 */
int modbus_master_write_coil(modbus_master_t *master, uint16_t addr, uint8_t on);

/*
 * 写单个保持寄存器（FC06）。
 * 参数：master 主站；addr 寄存器地址；value 16 位写入值。
 * 返回：0 成功；负值为本地错误；正值为从站异常码。
 */
int modbus_master_write_reg(modbus_master_t *master, uint16_t addr, uint16_t value);

/*
 * 写多个线圈（FC0F）。
 * 参数：master 主站；addr 起始地址；qty 数量（1..1968）；bits 按位打包数据。
 * 返回：0 成功；负值为本地错误；正值为从站异常码。
 */
int modbus_master_write_coils(modbus_master_t *master, uint16_t addr, uint16_t qty,
                              const uint8_t *bits);

/*
 * 写多个保持寄存器（FC10）。
 * 参数：master 主站；addr 起始地址；qty 数量（1..123）；regs 寄存器数组。
 * 返回：0 成功；负值为本地错误；正值为从站异常码。
 */
int modbus_master_write_regs(modbus_master_t *master, uint16_t addr, uint16_t qty,
                             const uint16_t *regs);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_MASTER_H */
