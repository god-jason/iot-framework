#ifndef MODBUS_SLAVE_H
#define MODBUS_SLAVE_H

/*
 * Modbus 从站：支持两种用法
 *   1) modbus_slave_if_t 回调接口（无状态，适合简单映射）；
 *   2) modbus_slave_t 寄存器映射表：按类型/地址注册连续区间，
 *      可直接映射到数组，也可绑定读/写回调（回调优先）。
 *
 * 处理流程：解帧 -> 地址过滤 -> 按功能码访问数据 -> 组异常或正常响应帧。
 * 广播（地址 0）请求会执行但不产生响应。
 */

#include "modbus.h"
#include "vec.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================= 从站（回调接口） ================= */

/* 从站数据访问接口：由应用实现，绑定具体线圈/寄存器映射。
 * 参数：addr 起始地址；qty 数量；bits/regs 数据缓冲；user 用户上下文。
 * 返回：0 成功；非 0 表示访问失败（从站返回相应异常码）。
 */
typedef int (*modbus_read_bits_fn)(uint16_t addr, uint16_t qty, uint8_t *bits, void *user);
typedef int (*modbus_read_regs_fn)(uint16_t addr, uint16_t qty, uint16_t *regs, void *user);
typedef int (*modbus_write_bits_fn)(uint16_t addr, const uint8_t *bits, uint16_t qty,
                                    void *user);
typedef int (*modbus_write_regs_fn)(uint16_t addr, const uint16_t *regs, uint16_t qty,
                                    void *user);

typedef struct {
    modbus_read_bits_fn  read_coils;      /* FC01：读线圈，bits 为打包位数据 */
    modbus_read_bits_fn  read_discrete;   /* FC02：读离散输入 */
    modbus_read_regs_fn  read_holding;    /* FC03：读保持寄存器 */
    modbus_read_regs_fn  read_input;      /* FC04：读输入寄存器 */
    modbus_write_bits_fn write_coils;     /* FC05 / FC0F：写线圈 */
    modbus_write_regs_fn write_regs;      /* FC06 / FC10：写寄存器 */
    void *user;                           /* 透传给上述回调的上下文 */
} modbus_slave_if_t;

/* 处理一条请求 ADU 并生成响应 ADU（含异常响应）。
 * 返回 0 成功（out_len 为响应长度）；返回负值表示输入帧非法（不应回复）。 */
int modbus_slave_process(modbus_mode_e mode, uint8_t slave,
                         const modbus_slave_if_t *iface,
                         const uint8_t *adu, uint32_t len,
                         uint8_t *out, uint32_t capacity, uint32_t *out_len);

/* ================= 从站（寄存器映射 + 回调） ================= */

/* 寄存器区类型：决定哪些功能码可访问、以及是否允许写 */
typedef enum {
    MODBUS_REG_COIL = 0,        /* FC01 / FC05 / FC0F，值 0/1 */
    MODBUS_REG_DISCRETE,        /* FC02，只读 */
    MODBUS_REG_HOLDING,         /* FC03 / FC06 / FC10 */
    MODBUS_REG_INPUT            /* FC04，只读 */
} modbus_reg_type_e;

/*
 * 读写回调：values 为 qty 个逻辑值（线圈/离散输入为 0/1）。
 * 返回 0 成功，非 0 表示访问失败（从站返回异常码 04）。
 */
typedef int (*modbus_slave_read_cb)(uint16_t addr, uint16_t *values, uint16_t qty, void *user);
typedef int (*modbus_slave_write_cb)(uint16_t addr, const uint16_t *values, uint16_t qty,
                                     void *user);

/* 寄存器区间描述：以下三种存储/回调方式任选，优先级 read_cb/write_cb > data > bits */
typedef struct {
    modbus_reg_type_e type;     /* 区间类型（线圈/离散/保持/输入） */
    uint16_t addr;              /* 起始地址 */
    uint16_t qty;               /* 数量（地址范围 [addr, addr+qty)） */

    uint16_t *data;             /* 直接映射的寄存器/值数组（长度 qty） */
    uint8_t *bits;              /* 线圈/离散输入的位数组（长度 qty，每字节 0/1） */

    modbus_slave_read_cb read_cb;   /* 读回调，非空时优先于 data/bits */
    modbus_slave_write_cb write_cb; /* 写回调，非空时优先于 data/bits */
    void *user;                     /* 回调上下文 */
} modbus_reg_range_t;

/* 映射表初始容量，可在编译时用 -D 覆盖 */
#ifndef MODBUS_SLAVE_MAX_RANGES
#define MODBUS_SLAVE_MAX_RANGES 16
#endif

typedef struct {
    uint8_t address;            /* 本机地址；0 表示广播模式（只处理不回复） */
    modbus_mode_e mode;         /* RTU / TCP */

    vec_t ranges;               /* 已注册区间表（变长数组，元素为 modbus_reg_range_t） */

    /* 统计 */
    uint32_t request_count;     /* 收到的有效请求数 */
    uint32_t exception_count;   /* 返回异常响应的次数 */
} modbus_slave_t;

/* 初始化从站（address 为 0 时按广播地址处理：处理请求但不回复） */
void modbus_slave_register_init(modbus_slave_t *slave, uint8_t address, modbus_mode_e mode);

/*
 * 注册连续寄存器/线圈区间（结构体整体拷贝，内部不持有指针生命周期管理）。
 * 参数：slave 从站；range 区间描述。
 * 返回：0 成功；MODBUS_ERR_PARAM 参数非法或地址越界；
 *       MODBUS_ERR_NO_MEMORY 内存不足。
 */
int modbus_slave_register_range(modbus_slave_t *slave, const modbus_reg_range_t *range);

/* 注册直接映射数组（regs 类型用 uint16 数组；coil/discrete 用 uint8 位数组） */
int modbus_slave_map_regs(modbus_slave_t *slave, uint16_t addr,
                          uint16_t *data, uint16_t qty, modbus_reg_type_e type);
int modbus_slave_map_bits(modbus_slave_t *slave, uint16_t addr,
                          uint8_t *bits, uint16_t qty, modbus_reg_type_e type);

/* 为已注册区间绑定读/写回调（回调优先于直接映射）
 * 返回：0 成功；MODBUS_ERR_PARAM 参数非法；MODBUS_ERR_RANGE 未找到匹配区间。 */
int modbus_slave_set_callback(modbus_slave_t *slave, modbus_reg_type_e type, uint16_t addr,
                              modbus_slave_read_cb read_cb, modbus_slave_write_cb write_cb,
                              void *user);

/* 注销指定类型/起始地址的区间，返回 0 或 MODBUS_ERR_RANGE。 */
int modbus_slave_unregister_range(modbus_slave_t *slave, modbus_reg_type_e type,
                                  uint16_t addr);

/* 处理一条请求 ADU：返回 0 成功（广播时 out_len=0）；
 * 负值表示帧非法 / 地址不匹配（不应回复）。 */
int modbus_slave_poll(modbus_slave_t *slave, const uint8_t *in, uint32_t in_len,
                      uint8_t *out, uint32_t out_capacity, uint32_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_SLAVE_H */
