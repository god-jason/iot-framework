#ifndef IOT_BASE64_H
#define IOT_BASE64_H

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 编码为 Base64（含 '=' 填充），output 以 '\0' 结尾，written 返回编码长度（不含 '\0'） */
int iot_base64_encode(const void *input, iot_size_t input_len,
                      char *output, iot_size_t output_size,
                      iot_size_t *written);

/* 解码 Base64，written 返回解码字节数 */
int iot_base64_decode(const char *input, iot_size_t input_len,
                      void *output, iot_size_t output_size,
                      iot_size_t *written);

#ifdef __cplusplus
}
#endif

#endif /* IOT_BASE64_H */