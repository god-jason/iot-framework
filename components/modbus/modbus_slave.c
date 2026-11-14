/*
 * Modbus 从站：回调接口与寄存器映射表两种实现。
 *
 * 两种用法共用同一套解帧/组帧逻辑：
 *   1) modbus_slave_process + modbus_slave_if_t：由应用回调直接提供数据；
 *   2) modbus_slave_poll + modbus_slave_t：数据来自预先注册的地址区间，
 *      区间可映射到数组，也可绑定读写回调（回调优先）。
 *
 * 请求处理顺序：解帧 -> 地址过滤 -> 功能码分发 -> 数据访问 -> 组响应。
 * 参数错误、地址未映射等返回带 0x80 标志的异常响应；帧非法则不回复。
 * 广播地址 0：执行写操作但不产生响应（out_len = 0）。
 */
#include "modbus.h"

#include <string.h>

/* ================= 从站（回调接口） ================= */

/*
 * 构造异常响应 PDU：[fc|0x80][异常码]。
 * 参数：fc 原功能码；code 异常码（MODBUS_EX_*）；pdu 输出缓冲（>= 2 字节）。
 * 返回：固定的响应 PDU 长度 2。
 */
static uint32_t slave_exception(uint8_t fc, uint8_t code, uint8_t *pdu)
{
    pdu[0] = (uint8_t)(fc | 0x80);
    pdu[1] = code;
    return 2;
}

/*
 * 按功能码处理请求 PDU，生成响应 PDU（位于 out，容量由上层保证）。
 * 参数：iface 数据访问接口；pdu/pdu_len 请求 PDU；out 响应缓冲；capacity 缓冲容量。
 * 返回：响应 PDU 长度；0 表示帧不完整、不应回复（也不返回异常）。
 * 说明：回调缺失返回 01，地址访问失败返回 02，数量/取值非法返回 03。
 */
static uint32_t slave_pdu(const modbus_slave_if_t *iface,
                          const uint8_t *pdu, uint32_t pdu_len,
                          uint8_t *out, uint32_t capacity)
{
    uint8_t fc;
    uint16_t addr;

    (void)capacity;

    if (pdu_len < 2 || iface == NULL) {
        return 0;
    }
    fc = pdu[0];
    addr = modbus_get_be16(&pdu[1]);

    switch (fc) {
    case MODBUS_FC_READ_COILS:
    case MODBUS_FC_READ_DISCRETE_INPUTS:
    case MODBUS_FC_READ_HOLDING_REGS:
    case MODBUS_FC_READ_INPUT_REGS: {
        uint8_t bits[256];
        uint16_t regs[125];
        uint16_t count;
        modbus_read_bits_fn rb;
        modbus_read_regs_fn rr;
        int ok;

        if (pdu_len < 5) {
            return 0;
        }
        /* 请求 PDU 布局：[fc][addr(2)][qty(2)] */
        count = modbus_get_be16(&pdu[3]);
        if (count == 0) {
            return slave_exception(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }

        if (fc == MODBUS_FC_READ_COILS) {
            rb = iface->read_coils;
            if (rb == NULL) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_FUNCTION, out);
            }
            ok = rb(addr, count, bits, iface->user);
            if (ok != 0) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_DATA_ADDR, out);
            }
            {
                uint32_t bytes = modbus_bits_to_bytes(count);
                uint32_t i;

                out[0] = fc;
                out[1] = (uint8_t)bytes;
                for (i = 0; i < bytes; i++) {
                    out[2 + i] = bits[i];
                }
                return 2 + bytes;
            }
        } else if (fc == MODBUS_FC_READ_DISCRETE_INPUTS) {
            rb = iface->read_discrete;
            if (rb == NULL) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_FUNCTION, out);
            }
            ok = rb(addr, count, bits, iface->user);
            if (ok != 0) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_DATA_ADDR, out);
            }
            {
                uint32_t bytes = modbus_bits_to_bytes(count);
                uint32_t i;

                out[0] = fc;
                out[1] = (uint8_t)bytes;
                for (i = 0; i < bytes; i++) {
                    out[2 + i] = bits[i];
                }
                return 2 + bytes;
            }
        } else if (fc == MODBUS_FC_READ_HOLDING_REGS) {
            rr = iface->read_holding;
            if (rr == NULL) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_FUNCTION, out);
            }
            ok = rr(addr, count, regs, iface->user);
            if (ok != 0) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_DATA_ADDR, out);
            }
            {
                uint32_t bytes = (uint32_t)count * 2;
                uint32_t i;

                out[0] = fc;
                out[1] = (uint8_t)bytes;
                for (i = 0; i < count; i++) {
                    modbus_put_be16(&out[2 + i * 2], regs[i]);
                }
                return 2 + bytes;
            }
        } else { /* FC04 */
            rr = iface->read_input;
            if (rr == NULL) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_FUNCTION, out);
            }
            ok = rr(addr, count, regs, iface->user);
            if (ok != 0) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_DATA_ADDR, out);
            }
            {
                uint32_t bytes = (uint32_t)count * 2;
                uint32_t i;

                out[0] = fc;
                out[1] = (uint8_t)bytes;
                for (i = 0; i < count; i++) {
                    modbus_put_be16(&out[2 + i * 2], regs[i]);
                }
                return 2 + bytes;
            }
        }
    }

    case MODBUS_FC_WRITE_SINGLE_COIL:
    case MODBUS_FC_WRITE_SINGLE_REG: {
        uint8_t bit;
        uint16_t reg;
        int ok;

        if (pdu_len < 5) {
            return 0;
        }
        if (fc == MODBUS_FC_WRITE_SINGLE_COIL) {
            if (iface->write_coils == NULL) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_FUNCTION, out);
            }
            /* 线圈写值：0xFF00 为 ON，0x0000 为 OFF（其它值按 OFF 容错处理） */
            bit = (modbus_get_be16(&pdu[3]) == 0xFF00) ? 1 : 0;
            ok = iface->write_coils(addr, &bit, 1, iface->user);
        } else {
            if (iface->write_regs == NULL) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_FUNCTION, out);
            }
            reg = modbus_get_be16(&pdu[3]);
            ok = iface->write_regs(addr, &reg, 1, iface->user);
        }
        if (ok != 0) {
            return slave_exception(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        memcpy(out, pdu, pdu_len);   /* 写单点：响应回显请求 */
        return pdu_len;
    }

    case MODBUS_FC_WRITE_MULTI_COILS:
    case MODBUS_FC_WRITE_MULTI_REGS: {
        uint8_t byte_count;
        uint16_t qty2;
        const uint8_t *vals;
        uint16_t n;
        int ok;

        if (pdu_len < 6) {
            return 0;
        }
        /* 多写 PDU 布局：[fc][addr(2)][qty(2)][byte_count][数据...] */
        qty2 = modbus_get_be16(&pdu[3]);
        byte_count = pdu[5];
        vals = &pdu[6];
        n = qty2;

        if (fc == MODBUS_FC_WRITE_MULTI_COILS) {
            if (iface->write_coils == NULL ||
                byte_count != modbus_bits_to_bytes(qty2)) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_FUNCTION, out);
            }
            ok = iface->write_coils(addr, vals, n, iface->user);
        } else {
            uint16_t regs[123];
            uint16_t count = (uint16_t)(byte_count / 2);
            uint32_t i;

            if (iface->write_regs == NULL || count != qty2) {
                return slave_exception(fc, MODBUS_EX_ILLEGAL_FUNCTION, out);
            }
            for (i = 0; i < count; i++) {
                regs[i] = modbus_get_be16(&vals[i * 2]);
            }
            ok = iface->write_regs(addr, regs, count, iface->user);
            (void)n;
        }
        if (ok != 0) {
            return slave_exception(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        out[0] = fc;
        modbus_put_be16(&out[1], addr);
        modbus_put_be16(&out[3], qty2);
        return 5;
    }

    default:
        return slave_exception(fc, MODBUS_EX_ILLEGAL_FUNCTION, out);
    }
}

/*
 * 回调接口从站：处理一条完整请求 ADU 并生成响应 ADU。
 * 参数：mode RTU/TCP；slave 本机地址；iface 数据访问接口；adu/len 请求帧；
 *       out/capacity 响应缓冲；out_len 输出响应长度。
 * 返回：0 成功；负值表示帧非法或地址不匹配（此时不应发送响应）。
 */
int modbus_slave_process(modbus_mode_e mode, uint8_t slave,
                         const modbus_slave_if_t *iface,
                         const uint8_t *adu, uint32_t len,
                         uint8_t *out, uint32_t capacity, uint32_t *out_len)
{
    uint8_t s;
    const uint8_t *pdu;
    uint32_t pdu_len;
    uint8_t resp[MODBUS_TCP_MAX_ADU - MODBUS_TCP_MBAP_LEN];
    uint32_t resp_len;
    uint32_t total;
    int r;

    if (iface == NULL || out == NULL || out_len == NULL) {
        return -1;
    }
    r = modbus_unframe_adu(mode, &s, adu, len, &pdu, &pdu_len);
    if (r != 0) {
        return r;
    }
    if (s != slave) {
        return -1;   /* 不是本机地址，不回复 */
    }

    resp_len = slave_pdu(iface, pdu, pdu_len, resp, sizeof(resp));
    if (resp_len == 0) {
        return -1;
    }

    total = modbus_frame_adu(mode, slave, resp, resp_len, out, capacity);
    if (total == 0) {
        return -1;
    }
    *out_len = total;
    return 0;
}

/* ================= 从站：寄存器映射表 ================= */

/* 批量访问区间的分块大小，限制单次回调栈上数组的规模 */
#define MODBUS_MAP_CHUNK 32

/*
 * 初始化映射式从站：清空区间表与统计。
 * 参数：slave 从站对象；address 本机地址（0 表示广播，只处理不回复）；
 *       mode RTU/TCP。
 */
void modbus_slave_register_init(modbus_slave_t *slave, uint8_t address, modbus_mode_e mode)
{
    if (slave == NULL) {
        return;
    }
    memset(slave, 0, sizeof(*slave));
    slave->address = address;
    slave->mode = mode;
    (void)vec_init(&slave->ranges, sizeof(modbus_reg_range_t),
                   MODBUS_SLAVE_MAX_RANGES);
}

/*
 * 追加一个地址区间（结构体按值拷贝）。
 * 参数：slave 从站；range 区间描述（qty 必须 > 0）。
 * 返回：0 成功；MODBUS_ERR_PARAM 参数非法或地址溢出 16 位空间；
 *       MODBUS_ERR_NO_MEMORY 内存不足。
 * 说明：不做重叠检查，查找时返回首个完全包含请求地址的区间。
 */
int modbus_slave_register_range(modbus_slave_t *slave, const modbus_reg_range_t *range)
{
    if (slave == NULL || range == NULL || range->qty == 0 ||
        (uint32_t)range->addr + range->qty > 0x10000u) {
        return MODBUS_ERR_PARAM;
    }
    if (vec_push(&slave->ranges, range) != 0) {
        return MODBUS_ERR_NO_MEMORY;
    }
    return 0;
}

/*
 * 注册一段直接映射的寄存器/值数组。
 * 参数：slave 从站；addr 起始地址；data 数组首地址（长度 >= qty）；
 *       qty 数量；type 区间类型。
 * 返回：同 modbus_slave_register_range。
 */
int modbus_slave_map_regs(modbus_slave_t *slave, uint16_t addr,
                          uint16_t *data, uint16_t qty, modbus_reg_type_e type)
{
    modbus_reg_range_t range;

    if (data == NULL) {
        return MODBUS_ERR_PARAM;
    }
    memset(&range, 0, sizeof(range));
    range.type = type;
    range.addr = addr;
    range.qty = qty;
    range.data = data;
    return modbus_slave_register_range(slave, &range);
}

/*
 * 注册一段直接映射的位数组（每字节存 0/1，每元素对应一个线圈/离散输入）。
 * 参数：slave 从站；addr 起始地址；bits 位数组（长度 >= qty）；qty 数量；
 *       type 区间类型（通常为 COIL 或 DISCRETE）。
 * 返回：同 modbus_slave_register_range。
 */
int modbus_slave_map_bits(modbus_slave_t *slave, uint16_t addr,
                          uint8_t *bits, uint16_t qty, modbus_reg_type_e type)
{
    modbus_reg_range_t range;

    if (bits == NULL) {
        return MODBUS_ERR_PARAM;
    }
    memset(&range, 0, sizeof(range));
    range.type = type;
    range.addr = addr;
    range.qty = qty;
    range.bits = bits;
    return modbus_slave_register_range(slave, &range);
}

/*
 * 为已注册区间绑定读写回调（回调优先于 data/bits 直接映射）。
 * 参数：slave 从站；type 区间类型；addr 区间起始地址（需与注册时一致）；
 *       read_cb/write_cb 读写回调（可置 NULL 表示不提供）；user 回调上下文。
 * 返回：0 成功；MODBUS_ERR_PARAM 参数非法；MODBUS_ERR_RANGE 未找到区间。
 */
int modbus_slave_set_callback(modbus_slave_t *slave, modbus_reg_type_e type, uint16_t addr,
                              modbus_slave_read_cb read_cb, modbus_slave_write_cb write_cb,
                              void *user)
{
    uint32_t i;

    if (slave == NULL) {
        return MODBUS_ERR_PARAM;
    }
    for (i = 0; i < vec_size(&slave->ranges); i++) {
        modbus_reg_range_t *r = (modbus_reg_range_t *)vec_at(&slave->ranges, i);
        if (r->type == type && r->addr == addr) {
            r->read_cb = read_cb;
            r->write_cb = write_cb;
            r->user = user;
            return 0;
        }
    }
    return MODBUS_ERR_RANGE;
}

/*
 * 注销一个区间：后续元素前移覆盖，区间数减一。
 * 参数：slave 从站；type 区间类型；addr 区间起始地址。
 * 返回：0 成功；MODBUS_ERR_PARAM 参数非法；MODBUS_ERR_RANGE 未找到区间。
 */
int modbus_slave_unregister_range(modbus_slave_t *slave, modbus_reg_type_e type,
                                  uint16_t addr)
{
    uint32_t i;

    if (slave == NULL) {
        return MODBUS_ERR_PARAM;
    }
    for (i = 0; i < vec_size(&slave->ranges); i++) {
        modbus_reg_range_t *r = (modbus_reg_range_t *)vec_at(&slave->ranges, i);
        if (r->type == type && r->addr == addr) {
            (void)vec_remove(&slave->ranges, i, NULL);
            return 0;
        }
    }
    return MODBUS_ERR_RANGE;
}

/*
 * 查找能完整覆盖 [addr, addr+qty) 的区间。
 * 参数：slave 从站；type 区间类型；addr 请求起始地址；qty 请求数量。
 * 返回：命中的区间指针；未命中或 qty == 0 时返回 NULL。
 * 说明：地址必须整体落在单个区间内，不支持跨区间拼接访问。
 */
static modbus_reg_range_t *map_find_range(modbus_slave_t *slave, modbus_reg_type_e type,
                                          uint16_t addr, uint16_t qty)
{
    uint32_t i;

    if (qty == 0) {
        return NULL;
    }
    for (i = 0; i < vec_size(&slave->ranges); i++) {
        modbus_reg_range_t *r = (modbus_reg_range_t *)vec_at(&slave->ranges, i);

        if (r->type != type) {
            continue;
        }
        if (addr < r->addr) {
            continue;
        }
        if ((uint32_t)addr + qty > (uint32_t)r->addr + r->qty) {
            continue;
        }
        return r;
    }
    return NULL;
}

/*
 * 从区间读取 qty 个值到 values。
 * 参数：r 命中区间；addr 起始地址；qty 数量；values 输出数组。
 * 返回：0 成功；-1 表示区间未配置任何数据源。
 * 优先级：read_cb > data > bits（bits 中的值转为 0/1）。
 */
static int map_read_values(modbus_reg_range_t *r, uint16_t addr, uint16_t qty,
                           uint16_t *values)
{
    uint16_t i;

    if (r->read_cb != NULL) {
        return r->read_cb(addr, values, qty, r->user);
    }
    if (r->data != NULL) {
        for (i = 0; i < qty; i++) {
            values[i] = r->data[(uint32_t)(addr - r->addr) + i];
        }
        return 0;
    }
    if (r->bits != NULL) {
        for (i = 0; i < qty; i++) {
            values[i] = r->bits[(uint32_t)(addr - r->addr) + i] ? 1U : 0U;
        }
        return 0;
    }
    return -1;
}

/*
 * 将 qty 个值写入区间。
 * 参数：r 命中区间；addr 起始地址；qty 数量；values 待写值（线圈按非 0 置 1）。
 * 返回：0 成功；-1 表示区间未配置任何可写数据源。
 * 优先级：write_cb > data > bits。
 */
static int map_write_values(modbus_reg_range_t *r, uint16_t addr, uint16_t qty,
                            const uint16_t *values)
{
    uint16_t i;

    if (r->write_cb != NULL) {
        return r->write_cb(addr, values, qty, r->user);
    }
    if (r->data != NULL) {
        for (i = 0; i < qty; i++) {
            r->data[(uint32_t)(addr - r->addr) + i] = values[i];
        }
        return 0;
    }
    if (r->bits != NULL) {
        for (i = 0; i < qty; i++) {
            r->bits[(uint32_t)(addr - r->addr) + i] = values[i] ? 1U : 0U;
        }
        return 0;
    }
    return -1;
}

/* 生成异常响应并累加异常计数，返回异常 PDU 长度（2）。 */
static uint32_t map_exception(modbus_slave_t *slave, uint8_t fc, uint8_t code,
                              uint8_t *out)
{
    slave->exception_count++;
    return slave_exception(fc, code, out);
}

/*
 * 处理寄存器映射从站的请求 PDU。
 * 参数：slave 从站；pdu/pdu_len 请求 PDU；out/capacity 响应缓冲及容量。
 * 返回：响应 PDU 长度；0 表示帧不完整或响应缓冲不足（不回复）。
 * 说明：所有访问都会先按类型/地址查找区间，未命中返回异常码 02；
 *       读操作按块访问（位最多 256 个/块，寄存器 125 个），
 *       写操作按 MODBUS_MAP_CHUNK 分块，避免大请求占用过多栈空间。
 */
static uint32_t map_pdu(modbus_slave_t *slave, const uint8_t *pdu, uint32_t pdu_len,
                        uint8_t *out, uint32_t capacity)
{
    uint8_t fc;
    uint16_t addr;
    modbus_reg_range_t *range;

    if (pdu_len < 2) {
        return 0;
    }
    fc = pdu[0];
    addr = modbus_get_be16(&pdu[1]);

    switch (fc) {
    case MODBUS_FC_READ_COILS:
    case MODBUS_FC_READ_DISCRETE_INPUTS: {
        uint16_t qty;
        uint16_t done = 0;
        uint32_t bytes;
        modbus_reg_type_e type = (fc == MODBUS_FC_READ_COILS)
                                 ? MODBUS_REG_COIL : MODBUS_REG_DISCRETE;

        if (pdu_len < 5) {
            return 0;
        }
        qty = modbus_get_be16(&pdu[3]);
        if (qty == 0 || qty > 2000) {
            return map_exception(slave, fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        range = map_find_range(slave, type, addr, qty);
        if (range == NULL) {
            return map_exception(slave, fc, MODBUS_EX_ILLEGAL_DATA_ADDR, out);
        }
        bytes = modbus_bits_to_bytes(qty);
        if (capacity < 2u + bytes) {
            return 0;
        }
        out[0] = fc;
        out[1] = (uint8_t)bytes;
        memset(&out[2], 0, bytes);
        while (done < qty) {
            uint16_t chunk = (uint16_t)((qty - done) > 256
                                        ? 256 : (qty - done));
            uint16_t i;
            uint16_t values[256];

            if (map_read_values(range, (uint16_t)(addr + done), chunk,
                                values) != 0) {
                return map_exception(slave, fc, MODBUS_EX_DEVICE_FAILURE, out);
            }
            /* 按位打包写入响应：bit 序号 done+i，字节内低位优先 */
            for (i = 0; i < chunk; i++) {
                if (values[i] & 1U) {
                    uint16_t bit = (uint16_t)(done + i);

                    out[2 + (bit >> 3)] |= (uint8_t)(1U << (bit & 7));
                }
            }
            done = (uint16_t)(done + chunk);
        }
        return 2u + bytes;
    }

    case MODBUS_FC_READ_HOLDING_REGS:
    case MODBUS_FC_READ_INPUT_REGS: {
        uint16_t qty;
        uint16_t i;
        uint16_t values[125];
        modbus_reg_type_e type = (fc == MODBUS_FC_READ_HOLDING_REGS)
                                 ? MODBUS_REG_HOLDING : MODBUS_REG_INPUT;

        if (pdu_len < 5) {
            return 0;
        }
        qty = modbus_get_be16(&pdu[3]);
        if (qty == 0 || qty > 125) {
            return map_exception(slave, fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        range = map_find_range(slave, type, addr, qty);
        if (range == NULL) {
            return map_exception(slave, fc, MODBUS_EX_ILLEGAL_DATA_ADDR, out);
        }
        if (map_read_values(range, addr, qty, values) != 0) {
            return map_exception(slave, fc, MODBUS_EX_DEVICE_FAILURE, out);
        }
        if (capacity < 2u + (uint32_t)qty * 2u) {
            return 0;
        }
        out[0] = fc;
        out[1] = (uint8_t)(qty * 2);
        for (i = 0; i < qty; i++) {
            modbus_put_be16(&out[2 + (uint32_t)i * 2], values[i]);
        }
        return 2u + (uint32_t)qty * 2u;
    }

    case MODBUS_FC_WRITE_SINGLE_COIL: {
        uint16_t raw;
        uint16_t value;

        if (pdu_len < 5) {
            return 0;
        }
        raw = modbus_get_be16(&pdu[3]);
        if (raw != 0xFF00 && raw != 0x0000) {
            return map_exception(slave, fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        range = map_find_range(slave, MODBUS_REG_COIL, addr, 1);
        if (range == NULL) {
            return map_exception(slave, fc, MODBUS_EX_ILLEGAL_DATA_ADDR, out);
        }
        value = (raw == 0xFF00) ? 1U : 0U;
        if (map_write_values(range, addr, 1, &value) != 0) {
            return map_exception(slave, fc, MODBUS_EX_DEVICE_FAILURE, out);
        }
        memcpy(out, pdu, 5);
        return 5;
    }

    case MODBUS_FC_WRITE_SINGLE_REG: {
        uint16_t value;

        if (pdu_len < 5) {
            return 0;
        }
        value = modbus_get_be16(&pdu[3]);
        range = map_find_range(slave, MODBUS_REG_HOLDING, addr, 1);
        if (range == NULL) {
            return map_exception(slave, fc, MODBUS_EX_ILLEGAL_DATA_ADDR, out);
        }
        if (map_write_values(range, addr, 1, &value) != 0) {
            return map_exception(slave, fc, MODBUS_EX_DEVICE_FAILURE, out);
        }
        memcpy(out, pdu, 5);
        return 5;
    }

    case MODBUS_FC_WRITE_MULTI_COILS: {
        uint16_t qty;
        uint8_t byte_count;
        const uint8_t *vals;
        uint16_t done = 0;

        if (pdu_len < 6) {
            return 0;
        }
        qty = modbus_get_be16(&pdu[3]);
        byte_count = pdu[5];
        if (qty == 0 || qty > 1968 ||
            byte_count != modbus_bits_to_bytes(qty) ||
            pdu_len < 6u + byte_count) {
            return map_exception(slave, fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        range = map_find_range(slave, MODBUS_REG_COIL, addr, qty);
        if (range == NULL) {
            return map_exception(slave, fc, MODBUS_EX_ILLEGAL_DATA_ADDR, out);
        }
        vals = &pdu[6];
        while (done < qty) {
            uint16_t chunk = (uint16_t)((qty - done) > MODBUS_MAP_CHUNK
                                        ? MODBUS_MAP_CHUNK : (qty - done));
            uint16_t i;
            uint16_t values[MODBUS_MAP_CHUNK];

            for (i = 0; i < chunk; i++) {
                uint16_t bit = (uint16_t)(done + i);

                values[i] = (vals[bit >> 3] >> (bit & 7)) & 1U;
            }
            if (map_write_values(range, (uint16_t)(addr + done), chunk,
                                 values) != 0) {
                return map_exception(slave, fc, MODBUS_EX_DEVICE_FAILURE, out);
            }
            done = (uint16_t)(done + chunk);
        }
        out[0] = fc;
        modbus_put_be16(&out[1], addr);
        modbus_put_be16(&out[3], qty);
        return 5;
    }

    case MODBUS_FC_WRITE_MULTI_REGS: {
        uint16_t qty;
        uint8_t byte_count;
        const uint8_t *vals;
        uint16_t done = 0;

        if (pdu_len < 6) {
            return 0;
        }
        qty = modbus_get_be16(&pdu[3]);
        byte_count = pdu[5];
        if (qty == 0 || qty > 123 ||
            byte_count != (uint8_t)(qty * 2) ||
            pdu_len < 6u + byte_count) {
            return map_exception(slave, fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        range = map_find_range(slave, MODBUS_REG_HOLDING, addr, qty);
        if (range == NULL) {
            return map_exception(slave, fc, MODBUS_EX_ILLEGAL_DATA_ADDR, out);
        }
        vals = &pdu[6];
        while (done < qty) {
            uint16_t chunk = (uint16_t)((qty - done) > MODBUS_MAP_CHUNK
                                        ? MODBUS_MAP_CHUNK : (qty - done));
            uint16_t i;
            uint16_t values[MODBUS_MAP_CHUNK];

            for (i = 0; i < chunk; i++) {
                values[i] = modbus_get_be16(&vals[(uint32_t)(done + i) * 2]);
            }
            if (map_write_values(range, (uint16_t)(addr + done), chunk,
                                 values) != 0) {
                return map_exception(slave, fc, MODBUS_EX_DEVICE_FAILURE, out);
            }
            done = (uint16_t)(done + chunk);
        }
        out[0] = fc;
        modbus_put_be16(&out[1], addr);
        modbus_put_be16(&out[3], qty);
        return 5;
    }

    default:
        return map_exception(slave, fc, MODBUS_EX_ILLEGAL_FUNCTION, out);
    }
}

/*
 * 映射式从站：处理一条请求 ADU 并生成响应 ADU。
 * 参数：slave 从站；in/in_len 请求帧；out/out_capacity 响应缓冲；
 *       out_len 输出响应长度（广播时为 0）。
 * 返回：0 成功；MODBUS_ERR_PARAM 参数非法或输出容量不足；
 *       MODBUS_ERR_FRAME 帧非法；MODBUS_ERR_RANGE 非本机地址（不回复）。
 */
int modbus_slave_poll(modbus_slave_t *slave, const uint8_t *in, uint32_t in_len,
                      uint8_t *out, uint32_t out_capacity, uint32_t *out_len)
{
    uint8_t addr;
    const uint8_t *pdu;
    uint32_t pdu_len;
    uint8_t resp[MODBUS_TCP_MAX_ADU - MODBUS_TCP_MBAP_LEN];
    uint32_t resp_len;
    uint32_t total;
    int r;

    if (slave == NULL || in == NULL || out == NULL || out_len == NULL) {
        return MODBUS_ERR_PARAM;
    }
    r = modbus_unframe_adu(slave->mode, &addr, in, in_len, &pdu, &pdu_len);
    if (r != 0) {
        return MODBUS_ERR_FRAME;
    }
    if (slave->address != 0 && addr != slave->address) {
        return MODBUS_ERR_RANGE;       /* 非本机地址，不回复 */
    }

    slave->request_count++;
    resp_len = map_pdu(slave, pdu, pdu_len, resp, sizeof(resp));
    if (resp_len == 0) {
        return MODBUS_ERR_FRAME;
    }

    if (slave->address == 0) {         /* 广播：处理但不回复 */
        *out_len = 0;
        return 0;
    }

    total = modbus_frame_adu(slave->mode, slave->address, resp, resp_len,
                             out, out_capacity);
    if (total == 0) {
        return MODBUS_ERR_PARAM;
    }
    *out_len = total;
    return 0;
}
