#ifndef IOT_BCD_H
#define IOT_BCD_H

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 将数值编码为压缩 BCD（每字节存两个十进制数字，高位在前） */
int iot_bcd_encode(uint32_t value, uint8_t *output, uint32_t output_len);

/* 将压缩 BCD 解码为数值；遇到非法 BCD（半字节 > 9）返回 IOT_ERR_INVALID */
int iot_bcd_decode(const uint8_t *input, uint32_t input_len, uint32_t *value);

/* 校验数据是否为合法 BCD */
int iot_bcd_is_valid(const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* IOT_BCD_H */