#ifndef MAX6675_REGISTER_H
#define MAX6675_REGISTER_H

/* MAX6675 K 型热电偶转换器（SPI 只读，16 位输出） */

/* 16 位读出格式
 *   bit15      : 恒为 0（伪签名位）
 *   bit14 - 3  : 温度，12 位，1 LSB = 0.25 ℃
 *   bit2       : 1 表示热电偶开路
 *   bit1       : 恒为 0（器件 ID）
 *   bit0       : 恒为 1（三态，读为 0）
 */
#define MAX6675_DATA_MASK           0x0004U   /* bit2 开路标志 */
#define MAX6675_RESERVED_MASK       0x0003U   /* bit1 恒 0，bit0 恒 0 */
#define MAX6675_TEMP_SHIFT          3U
#define MAX6675_LSB_CELSIUS         0.25f

/* 转换时间：典型 170ms，保守 220ms */
#define MAX6675_CONV_DELAY_MS       220U

#define MAX6675_FRAME_SIZE          2U

#endif /* MAX6675_REGISTER_H */
