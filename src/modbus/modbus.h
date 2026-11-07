#ifndef MODBUS_H
#define MODBUS_H

/*
 * Modbus 协议（RTU / TCP）：完整主站与从站。
 * 寄存器/线圈地址与数值均按大端，RTU 帧尾 CRC16/MODBUS（低字节在前），
 * TCP 使用 MBAP 头（长度域为字节数，大端）。
 *
 * 主站：通过 transport 回调完成一帧收发，内置请求组帧、响应解析与异常识别。
 * 从站：支持两种用法
 *   1) modbus_slave_if_t 回调接口（无状态，适合简单映射）；
 *   2) modbus_slave_t 寄存器映射表：按类型/地址注册连续区间，
 *      可直接映射到数组，也可绑定读/写回调（回调优先）。
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

/* 主站返回值（负值）；返回值 > 0 时表示 Modbus 异常码 */
#define MODBUS_ERR_PARAM               (-1)
#define MODBUS_ERR_TRANSPORT           (-2)
#define MODBUS_ERR_FRAME               (-3)
#define MODBUS_ERR_RANGE               (-4)
#define MODBUS_ERR_NO_MEMORY           (-5)

/* TCP MBAP 头 7 字节，其中长度域（大端）为后续字节数（unit + PDU） */
#define MODBUS_TCP_MBAP_LEN            7
#define MODBUS_TCP_MAX_ADU             260

int modbus_init(void);

uint16_t modbus_crc16(const uint8_t *data, uint32_t len);

/* ================= 主站（无状态帧工具） =================
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

/* ================= 主站（带传输与寄存器读写） ================= */
/*
 * 传输回调：发送 tx 并等待一帧响应写入 rx。
 * 返回 0 表示成功；非 0 表示链路错误。
 */
typedef int (*modbus_transport_fn)(void *ctx, const uint8_t *tx, uint32_t tx_len,
                                   uint8_t *rx, uint32_t rx_capacity,
                                   uint32_t *rx_len, uint32_t timeout_ms);

typedef struct {
    modbus_mode_t mode;
    uint8_t address;            /* 从站地址 */
    uint16_t transaction_id;    /* TCP 事务号，自动递增 */
    uint32_t timeout_ms;

    modbus_transport_fn transport;
    void *user;

    /* 统计 */
    uint32_t request_count;
    uint32_t error_count;
} modbus_master_t;

/* 初始化主站。transport 可稍后用 modbus_master_set_transport 设置。 */
int modbus_master_init(modbus_master_t *master, modbus_mode_t mode, uint8_t address,
                       modbus_transport_fn transport, void *user, uint32_t timeout_ms);
void modbus_master_set_transport(modbus_master_t *master, modbus_transport_fn transport, void *user);
void modbus_master_set_address(modbus_master_t *master, uint8_t address);

/* 读：bits 为按位打包缓冲（长度 >= (qty+7)/8）；regs 为 qty 个 uint16。
 * 返回 0 成功；负值见 MODBUS_ERR_*；正值表示 Modbus 异常码。 */
int modbus_master_read_coils(modbus_master_t *master, uint16_t addr, uint16_t qty, uint8_t *bits);
int modbus_master_read_discrete(modbus_master_t *master, uint16_t addr, uint16_t qty, uint8_t *bits);
int modbus_master_read_holding(modbus_master_t *master, uint16_t addr, uint16_t qty, uint16_t *regs);
int modbus_master_read_input(modbus_master_t *master, uint16_t addr, uint16_t qty, uint16_t *regs);

/* 写：单点 / 多点（bits 为按位打包缓冲） */
int modbus_master_write_coil(modbus_master_t *master, uint16_t addr, uint8_t on);
int modbus_master_write_reg(modbus_master_t *master, uint16_t addr, uint16_t value);
int modbus_master_write_coils(modbus_master_t *master, uint16_t addr, uint16_t qty,
                              const uint8_t *bits);
int modbus_master_write_regs(modbus_master_t *master, uint16_t addr, uint16_t qty,
                             const uint16_t *regs);

/* ================= 从站（回调接口） ================= */
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

/* ================= 从站（寄存器映射 + 回调） ================= */
typedef enum {
    MODBUS_REG_COIL = 0,        /* FC01 / FC05 / FC0F，值 0/1 */
    MODBUS_REG_DISCRETE,        /* FC02，只读 */
    MODBUS_REG_HOLDING,         /* FC03 / FC06 / FC10 */
    MODBUS_REG_INPUT            /* FC04，只读 */
} modbus_reg_type_t;

/*
 * 读写回调：values 为 qty 个逻辑值（线圈/离散输入为 0/1）。
 * 返回 0 成功，非 0 表示访问失败（从站返回异常码 04）。
 */
typedef int (*modbus_slave_read_cb)(uint16_t addr, uint16_t *values, uint16_t qty, void *user);
typedef int (*modbus_slave_write_cb)(uint16_t addr, const uint16_t *values, uint16_t qty, void *user);

/* 寄存器区间描述：以下三种存储/回调方式任选，优先级 read_cb/write_cb > data > bits */
typedef struct {
    modbus_reg_type_t type;
    uint16_t addr;              /* 起始地址 */
    uint16_t qty;               /* 数量 */

    uint16_t *data;             /* 直接映射的寄存器/值数组（长度 qty） */
    uint8_t *bits;              /* 线圈/离散输入的位数组（长度 qty，每字节 0/1） */

    modbus_slave_read_cb read_cb;
    modbus_slave_write_cb write_cb;
    void *user;
} modbus_reg_range_t;

#ifndef MODBUS_SLAVE_MAX_RANGES
#define MODBUS_SLAVE_MAX_RANGES 16
#endif

typedef struct {
    uint8_t address;
    modbus_mode_t mode;

    modbus_reg_range_t ranges[MODBUS_SLAVE_MAX_RANGES];
    uint16_t range_count;

    /* 统计 */
    uint32_t request_count;
    uint32_t exception_count;
} modbus_slave_t;

/* 初始化从站（address 为 0 时按广播地址处理：处理请求但不回复） */
void modbus_slave_register_init(modbus_slave_t *slave, uint8_t address, modbus_mode_t mode);

/* 注册连续寄存器/线圈区间，返回 0 成功 */
int modbus_slave_register_range(modbus_slave_t *slave, const modbus_reg_range_t *range);

/* 注册直接映射数组（regs 类型用 uint16 数组；coil/discrete 用 uint8 位数组） */
int modbus_slave_map_regs(modbus_slave_t *slave, uint16_t addr,
                          uint16_t *data, uint16_t qty, modbus_reg_type_t type);
int modbus_slave_map_bits(modbus_slave_t *slave, uint16_t addr,
                          uint8_t *bits, uint16_t qty, modbus_reg_type_t type);

/* 为已注册区间绑定读/写回调（回调优先于直接映射） */
int modbus_slave_set_callback(modbus_slave_t *slave, modbus_reg_type_t type, uint16_t addr,
                              modbus_slave_read_cb read_cb, modbus_slave_write_cb write_cb,
                              void *user);

/* 注销指定类型/起始地址的区间 */
int modbus_slave_unregister_range(modbus_slave_t *slave, modbus_reg_type_t type, uint16_t addr);

/* 处理一条请求 ADU：返回 0 成功（广播时 out_len=0）；
 * 负值表示帧非法 / 地址不匹配（不应回复）。 */
int modbus_slave_poll(modbus_slave_t *slave, const uint8_t *in, uint32_t in_len,
                      uint8_t *out, uint32_t out_capacity, uint32_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_H */
