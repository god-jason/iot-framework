/*
 * endian.c - 字节序转换与多字节数值读写
 *
 * 模块职责：
 *   提供 16/32/64 位整数的字节序翻转，以及按小端（LE）或大端（BE）
 *   从字节流组装数值、把数值拆回字节流的能力，用于协议帧编解码。
 *
 * 数据流与依赖：
 *   纯计算，按字节逐个操作，与宿主机字节序无关，无全局状态、无对齐要求。
 *
 * 边界约束：
 *   - 所有 read_* / write_* 函数都要求 data 指向足够长的缓冲区
 *     （2/4/8 字节），本模块不做长度检查。
 *   - data 为 NULL 时读取函数返回 0，写入函数直接返回（静默忽略）。
 *   - bswap 系列仅翻转字节序，不涉及主机端序判断，可在任意平台使用。
 */

#include "endian.h"

/*
 * 16 位字节序翻转。
 * 参数：value - 原值；返回：高低字节互换后的值
 */
uint16_t bswap16(uint16_t value)
{
    return (uint16_t)((value >> 8) | (value << 8));
}

/*
 * 32 位字节序翻转。
 * 参数：value - 原值；返回：4 个字节逆序后的值
 * 注意：每一步先右移再掩码，确保移位产生的多余位被清除。
 */
uint32_t bswap32(uint32_t value)
{
    return ((value >> 24) & 0x000000FFu) |
           ((value >> 8)  & 0x0000FF00u) |
           ((value << 8)  & 0x00FF0000u) |
           ((value << 24) & 0xFF000000u);
}

/*
 * 64 位字节序翻转。
 * 参数：value - 原值；返回：8 个字节逆序后的值
 * 注意：8 个字节两两配对搬运，掩码常量必须带 ULL 后缀避免溢出。
 */
uint64_t bswap64(uint64_t value)
{
    return ((value >> 56) & 0x00000000000000FFULL) |
           ((value >> 40) & 0x000000000000FF00ULL) |
           ((value >> 24) & 0x0000000000FF0000ULL) |
           ((value >> 8)  & 0x00000000FF000000ULL) |
           ((value << 8)  & 0x000000FF00000000ULL) |
           ((value << 24) & 0x0000FF0000000000ULL) |
           ((value << 40) & 0x00FF000000000000ULL) |
           ((value << 56) & 0xFF00000000000000ULL);
}

/*
 * 以 little-endian 读取 16 位无符号数。
 * 参数：data - 至少 2 字节，data[0] 为低字节
 * 返回：组装结果；data 为 NULL 时返回 0
 */
uint16_t read_le16(const uint8_t *data)
{
    if (data == NULL) {
        return 0;
    }
    return (uint16_t)(data[0] | (data[1] << 8));
}

/*
 * 以 little-endian 读取 32 位无符号数。
 * 参数：data - 至少 4 字节，data[0] 为最低字节
 * 返回：组装结果；data 为 NULL 时返回 0
 */
uint32_t read_le32(const uint8_t *data)
{
    if (data == NULL) {
        return 0;
    }
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

/*
 * 以 little-endian 读取 64 位无符号数。
 * 参数：data - 至少 8 字节，data[0] 为最低字节
 * 返回：组装结果；data 为 NULL 时返回 0
 */
uint64_t read_le64(const uint8_t *data)
{
    uint64_t v = 0;
    int i;
    if (data == NULL) {
        return 0;
    }
    for (i = 0; i < 8; i++) {
        /* 先把字节提升到 64 位再移位，避免中途截断 */
        v |= ((uint64_t)data[i]) << (i * 8);
    }
    return v;
}

/*
 * 以 big-endian 读取 16 位无符号数。
 * 参数：data - 至少 2 字节，data[0] 为高字节
 * 返回：组装结果；data 为 NULL 时返回 0
 */
uint16_t read_be16(const uint8_t *data)
{
    if (data == NULL) {
        return 0;
    }
    return (uint16_t)((data[0] << 8) | data[1]);
}

/*
 * 以 big-endian 读取 32 位无符号数。
 * 参数：data - 至少 4 字节，data[0] 为最高字节
 * 返回：组装结果；data 为 NULL 时返回 0
 */
uint32_t read_be32(const uint8_t *data)
{
    if (data == NULL) {
        return 0;
    }
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           ((uint32_t)data[3]);
}

/*
 * 以 big-endian 读取 64 位无符号数。
 * 参数：data - 至少 8 字节，data[0] 为最高字节
 * 返回：组装结果；data 为 NULL 时返回 0
 */
uint64_t read_be64(const uint8_t *data)
{
    uint64_t v = 0;
    int i;
    if (data == NULL) {
        return 0;
    }
    for (i = 0; i < 8; i++) {
        v = (v << 8) | data[i];   /* 逐字节左移拼接 */
    }
    return v;
}

/*
 * 以小端格式写入 16 位数值。
 * 参数：data - 至少 2 字节的目标缓冲；value - 待写入值
 * 返回：无；data 为 NULL 时直接返回
 */
void write_le16(uint8_t *data, uint16_t value)
{
    if (data == NULL) {
        return;
    }
    data[0] = (uint8_t)(value & 0xFF);         /* 低字节在前 */
    data[1] = (uint8_t)((value >> 8) & 0xFF);
}

/*
 * 以小端格式写入 32 位数值。
 * 参数：data - 至少 4 字节的目标缓冲；value - 待写入值
 * 返回：无；data 为 NULL 时直接返回
 */
void write_le32(uint8_t *data, uint32_t value)
{
    if (data == NULL) {
        return;
    }
    data[0] = (uint8_t)(value & 0xFF);
    data[1] = (uint8_t)((value >> 8) & 0xFF);
    data[2] = (uint8_t)((value >> 16) & 0xFF);
    data[3] = (uint8_t)((value >> 24) & 0xFF);
}

/*
 * 以小端格式写入 64 位数值。
 * 参数：data - 至少 8 字节的目标缓冲；value - 待写入值
 * 返回：无；data 为 NULL 时直接返回
 */
void write_le64(uint8_t *data, uint64_t value)
{
    int i;
    if (data == NULL) {
        return;
    }
    for (i = 0; i < 8; i++) {
        data[i] = (uint8_t)((value >> (i * 8)) & 0xFF);
    }
}

/*
 * 以大端格式写入 16 位数值。
 * 参数：data - 至少 2 字节的目标缓冲；value - 待写入值
 * 返回：无；data 为 NULL 时直接返回
 */
void write_be16(uint8_t *data, uint16_t value)
{
    if (data == NULL) {
        return;
    }
    data[0] = (uint8_t)((value >> 8) & 0xFF);   /* 高字节在前 */
    data[1] = (uint8_t)(value & 0xFF);
}

/*
 * 以大端格式写入 32 位数值。
 * 参数：data - 至少 4 字节的目标缓冲；value - 待写入值
 * 返回：无；data 为 NULL 时直接返回
 */
void write_be32(uint8_t *data, uint32_t value)
{
    if (data == NULL) {
        return;
    }
    data[0] = (uint8_t)((value >> 24) & 0xFF);
    data[1] = (uint8_t)((value >> 16) & 0xFF);
    data[2] = (uint8_t)((value >> 8) & 0xFF);
    data[3] = (uint8_t)(value & 0xFF);
}

/*
 * 以大端格式写入 64 位数值。
 * 参数：data - 至少 8 字节的目标缓冲；value - 待写入值
 * 返回：无；data 为 NULL 时直接返回
 */
void write_be64(uint8_t *data, uint64_t value)
{
    int i;
    if (data == NULL) {
        return;
    }
    for (i = 0; i < 8; i++) {
        /* 第 i 个字节取从高往低第 i 个 8 位段 */
        data[i] = (uint8_t)((value >> ((7 - i) * 8)) & 0xFF);
    }
}
