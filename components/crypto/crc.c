/*
 * CRC 校验模块
 *
 * 职责：
 *   提供按位（bit-by-bit）计算的通用 CRC 内核，并封装常用的
 *   CRC-8 / CRC-16/MODBUS / CRC-16/CCITT-FALSE / CRC-32 变体。
 *
 * 数据流：
 *   调用者传入数据指针与长度 -> crc_calculate() 按配置逐字节、逐位迭代
 *   -> 返回（可选输入/输出反射与输出异或后的）校验值。
 *
 * 依赖与约束：
 *   - 仅依赖 标准头文件（uint*_t / bool），无动态内存、无全局状态，可重入；
 *   - 不缓存查表，时间复杂度 O(len * 8)，适合固件校验等非高频场景；
 *   - 位宽限定 1..32，超出范围或指针非法时返回 0；
 *   - 所有中间结果都与 width 位掩码相与，避免高位脏数据影响位移。
 */
#include "crc.h"

/*
 * 将 value 的低 width 位按位镜像反转（bit0 <-> bit(width-1)）。
 * 参数：value 待反转的值；width 有效位宽（1..32）。
 * 返回：反转后的值，仅低 width 位有意义。
 * 用途：反射型算法需先把 MSB 形式的多项式转换成 LSB 形式。
 */
static uint32_t crc_reflect(uint32_t value, uint8_t width)
{
    uint32_t r = 0;
    uint8_t i;
    for (i = 0; i < width; i++) {
        /* 取出第 i 位，搬到对称位置 width-1-i 上 */
        if (value & (1u << i)) {
            r |= (1u << (width - 1 - i));
        }
    }
    return r;
}

/*
 * 通用 CRC 计算。
 * 参数：
 *   config - CRC 参数（多项式/初值/位宽/反射标志/输出异或），不可为 NULL；
 *   data   - 待校验数据；len 为 0 时允许为 NULL；
 *   len    - 数据字节数。
 * 返回：计算结果（已按 width 掩码），参数非法时返回 0。
 * 说明：
 *   - reflect_in 为真时采用 LSB 优先（反射）算法，否则 MSB 优先；
 *   - reflect_out 为真时在异或输出前对结果再做一次按位反转；
 *   - 该函数不做流式处理，每次调用均从 config->initial 重新开始。
 */
uint32_t crc_calculate(const crc_config_t *config,
                           const void *data,
                           uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc, mask, poly;
    uint32_t i;

    if (config == NULL || (data == NULL && len != 0)) {
        return 0;
    }
    if (config->width > 32 || config->width == 0) {
        return 0;
    }

    mask = (config->width >= 32) ? 0xFFFFFFFFu : ((1u << config->width) - 1u);
    /* 初值先截断到位宽范围，防止高位干扰 */
    crc = config->initial & mask;

    if (config->reflect_in) {
        /* 反射算法：LSB 优先，多项式需先镜像为 LSB 形式 */
        poly = crc_reflect(config->polynomial, config->width) & mask;
        for (i = 0; i < len; i++) {
            uint8_t bit;
            /* 每个字节先并入累加器最低 8 位 */
            crc ^= p[i];
            for (bit = 0; bit < 8; bit++) {
                /* 最低位为 1 时右移后异或多项式，否则仅右移 */
                if (crc & 1u) {
                    crc = (crc >> 1) ^ poly;
                } else {
                    crc = (crc >> 1);
                }
            }
            crc &= mask;   /* 每处理完一个字节截断到位宽 */
        }
    } else {
        /* 常规算法：MSB 优先 */
        uint32_t topbit = 1u << (config->width - 1);   /* 位宽内的最高位 */
        poly = config->polynomial & mask;
        for (i = 0; i < len; i++) {
            uint8_t bit;
            /* 字节对齐到 width 位的高 8 位参与运算 */
            crc ^= ((uint32_t)p[i]) << (config->width - 8);
            for (bit = 0; bit < 8; bit++) {
                /* 最高位为 1 时左移后异或多项式，否则仅左移 */
                if (crc & topbit) {
                    crc = (crc << 1) ^ poly;
                } else {
                    crc = (crc << 1);
                }
            }
            crc &= mask;   /* 每处理完一个字节截断到位宽 */
        }
    }

    if (config->reflect_out) {
        /* 输出反射（对应 CRC 参数中的 refout） */
        crc = crc_reflect(crc, config->width);
    }

    /* 与输出异或值相异或，并再次截断到位宽 */
    return (crc ^ config->xor_out) & mask;
}

/*
 * CRC-8（poly 0x07，init 0x00，不反射，xorout 0x00）。
 * 参数：data 数据指针（len 为 0 时可为 NULL）；len 字节数。
 * 返回：8 位校验值。
 */
uint8_t crc8(const void *data, uint32_t len)
{
    crc_config_t cfg;
    cfg.polynomial = 0x07u;
    cfg.initial    = 0x00u;
    cfg.xor_out    = 0x00u;
    cfg.width      = 8;
    cfg.reflect_in = false;
    cfg.reflect_out = false;
    return (uint8_t)crc_calculate(&cfg, data, len);
}

/*
 * CRC-16/MODBUS（poly 0x8005，init 0xFFFF，输入反射，xorout 0x0000）。
 * 参数：data 数据指针；len 字节数。
 * 返回：16 位校验值，常用于 Modbus RTU 帧尾校验。
 */
uint16_t crc16_modbus(const void *data, uint32_t len)
{
    crc_config_t cfg;
    cfg.polynomial = 0x8005u;
    cfg.initial    = 0xFFFFu;
    cfg.xor_out    = 0x0000u;
    cfg.width      = 16;
    cfg.reflect_in = true;
    cfg.reflect_out = false;
    return (uint16_t)crc_calculate(&cfg, data, len);
}

/*
 * CRC-16/CCITT-FALSE（poly 0x1021，init 0xFFFF，不反射，xorout 0x0000）。
 * 参数：data 数据指针；len 字节数。
 * 返回：16 位校验值。
 */
uint16_t crc16_ccitt(const void *data, uint32_t len)
{
    crc_config_t cfg;
    cfg.polynomial = 0x1021u;
    cfg.initial    = 0xFFFFu;
    cfg.xor_out    = 0x0000u;
    cfg.width      = 16;
    cfg.reflect_in = false;
    cfg.reflect_out = false;
    return (uint16_t)crc_calculate(&cfg, data, len);
}

/*
 * CRC-32（poly 0x04C11DB7，init/xorout 0xFFFFFFFF，输入反射）。
 * 参数：data 数据指针；len 字节数。
 * 返回：32 位校验值。
 * 说明：reflect_in 为真时内部已把多项式镜像并按 LSB 优先处理，
 *       该配置得到的即通用 CRC-32 结果（"123456789" -> 0xCBF43926），
 *       再置 reflect_out 会额外镜像一次，仅在特殊协议中才需要。
 */
uint32_t crc32(const void *data, uint32_t len)
{
    crc_config_t cfg;
    cfg.polynomial = 0x04C11DB7u;
    cfg.initial    = 0xFFFFFFFFu;
    cfg.xor_out    = 0xFFFFFFFFu;
    cfg.width      = 32;
    cfg.reflect_in = true;
    cfg.reflect_out = false;
    return crc_calculate(&cfg, data, len);
}