#ifndef CRC_H
#define CRC_H

/*
 * CRC 校验头文件。
 *
 * 模块职责：声明通用 CRC 配置结构、通用计算接口以及 4 个常用变体。
 * 数据依赖：仅依赖 标准头文件 中的定宽整型与 bool。
 * 使用约束：
 *   - 全部接口为无状态纯函数，可重入、可在中断上下文中调用；
 *   - 数据长度按字节计，不做流式累加，每次调用独立完成；
 *   - 计算结果已按 width 截断，宽位变体返回值需由调用者再转换类型。
 *
 * 命名函数采用广泛使用的标准配置：
 *   crc8          -> CRC-8 (poly 0x07, init 0x00)
 *   crc16_modbus  -> CRC-16/MODBUS (poly 0x8005, init 0xFFFF, 反射)
 *   crc16_ccitt   -> CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)
 *   crc32         -> CRC-32/IEEE 802.3 (poly 0x04C11DB7, init/xorout 0xFFFFFFFF)
 * 固件完整性校验建议使用 crc32；需要更多变体时可用 crc_calculate()。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CRC 算法参数集合，字段含义遵循常见 CRC 参数模型（width/poly/init/refin/refout/xorout） */
typedef struct {
    uint32_t polynomial;   /* 生成多项式（按 MSB 形式书写，反射时内部自动镜像） */
    uint32_t initial;      /* 初值，计算前会被截断到 width 位 */
    uint32_t xor_out;      /* 输出异或值（最终结果与它相异或） */
    uint8_t  width;        /* 位宽：8 / 16 / 32，合法范围 1..32 */
    bool     reflect_in;   /* 输入字节是否按位反转（为真时采用 LSB 优先算法） */
    bool     reflect_out;  /* 输出是否再按位反转（对应 refout） */
} crc_config_t;

/*
 * 计算 CRC-8：poly 0x07，init 0x00，不反射。
 * 参数：data 数据指针（len 为 0 时可为 NULL）；len 字节数。
 * 返回：8 位校验值；参数非法时返回 0。
 */
uint8_t  crc8(const void *data, uint32_t len);

/*
 * 计算 CRC-16/MODBUS：poly 0x8005，init 0xFFFF，输入反射。
 * 参数：data 数据指针；len 字节数。
 * 返回：16 位校验值；参数非法时返回 0。
 */
uint16_t crc16_modbus(const void *data, uint32_t len);

/*
 * 计算 CRC-16/CCITT-FALSE：poly 0x1021，init 0xFFFF，不反射。
 * 参数：data 数据指针；len 字节数。
 * 返回：16 位校验值；参数非法时返回 0。
 */
uint16_t crc16_ccitt(const void *data, uint32_t len);

/*
 * 计算 CRC-32：poly 0x04C11DB7，init/xorout 0xFFFFFFFF，输入反射。
 * 参数：data 数据指针；len 字节数。
 * 返回：32 位校验值；参数非法时返回 0。
 * 说明：reflect_in 为真时已按 LSB 优先计算，结果与通用实现一致
 *       （标准校验串 "123456789" 为 0xCBF43926）；reflect_out 会再镜像。
 */
uint32_t crc32(const void *data, uint32_t len);

/*
 * 按自定义配置计算任意 1..32 位 CRC。
 * 参数：config 参数结构指针，不可为 NULL；data 数据指针；
 *       len 数据字节数（len 为 0 时 data 可为 NULL）。
 * 返回：计算结果（已按 width 掩码）；config 非法、width 越界或
 *       data 非法时返回 0。
 */
uint32_t crc_calculate(const crc_config_t *config,
                           const void *data,
                           uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* CRC_H */