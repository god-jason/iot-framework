#ifndef IOT_HEX_H
#define IOT_HEX_H

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 将字节流编码为十六进制字符串；output 会以 '\0' 结尾 */
int iot_hex_encode(const void *input, iot_size_t input_len,
                   char *output, iot_size_t output_size,
                   bool uppercase);

/* 将十六进制字符串解码为字节流；written 返回实际解码字节数 */
int iot_hex_decode(const char *input, iot_size_t input_len,
                   void *output, iot_size_t output_size,
                   iot_size_t *written);

#ifdef __cplusplus
}
#endif

#endif /* IOT_HEX_H */