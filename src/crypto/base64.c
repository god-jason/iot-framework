/*
 * base64.c - Base64 编解码实现（RFC 4648 标准字母表 A-Z a-z 0-9 + /）
 *
 * 模块职责：
 *   为上层（HTTP 认证头、MQTT 载荷、AT 指令参数等）提供纯软件的
 *   Base64 文本化与还原能力，不依赖堆内存和平台接口。
 *
 * 数据流与依赖：
 *   仅依赖 标准头文件 的基础类型与返回码；输入由调用者给出，
 *   结果写入调用者提供的缓冲区。模块内部无全局状态，可重入
 *   （多个执行流共用同一输出缓冲区时需调用者自行串行化）。
 *
 * 边界约束：
 *   - 编码所需输出空间（含结尾 '\0'）为 ((input_len + 2) / 3) * 4 + 1，
 *     空间不足时返回 -1 且不写任何数据。
 *   - 解码输出空间不足时返回 -1，此时可能已写出前面部分字节。
 *   - 仅支持标准字母表与 '=' 填充；不支持 URL 安全字母表（'-'、'_'），
 *     也不校验填充比特必须为 0 等规范性要求。
 */

#include "base64.h"

/* Base64 标准字母表：下标 0..63 依次对应 A-Z、a-z、0-9、'+'、'/' */
static const char b64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/*
 * 将单个 Base64 字符映射为 6 bit 数值（0..63）。
 * 参数：c - 待转换字符
 * 返回：0..63 表示有效字符；-1 表示非法字符（含 '=' 与空白字符）
 */
static int b64_value(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';          /* 'A'..'Z' -> 0..25 */
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;     /* 'a'..'z' -> 26..51 */
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;     /* '0'..'9' -> 52..61 */
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

/*
 * Base64 编码（含 '=' 填充）。
 * 参数：
 *   input       - 待编码数据首地址，不可为 NULL（即使 input_len 为 0）
 *   input_len   - 待编码字节数
 *   output      - 输出文本缓冲，成功时以 '\0' 结尾
 *   output_size - 输出缓冲容量，至少为 ((input_len + 2) / 3) * 4 + 1
 *   written     - 可选（可为 NULL），返回编码字符数（不含结尾 '\0'）
 * 返回：0 成功；-1 参数非法或输出空间不足
 * 注意：每 3 字节输入对应 4 个字符；末尾不足 3 字节时用 '=' 补齐。
 */
int base64_encode(const void *input, size_t input_len,
                      char *output, size_t output_size,
                      size_t *written)
{
    const uint8_t *src = (const uint8_t *)input;
    size_t i, out = 0;
    /* 向上取整到 3 字节的整数倍，再换算成 4/3 的字符数 */
    size_t needed = ((input_len + 2u) / 3u) * 4u;

    if (input == NULL || output == NULL) {
        return -1;
    }
    /* 需要容纳 needed 个字符外加结尾 '\0' */
    if (output_size < needed + 1u) {
        return -1;
    }

    for (i = 0; i < input_len; i += 3) {
        /* v 按大端顺序装入最多 3 个输入字节：bits23..16 / 15..8 / 7..0 */
        uint32_t v = ((uint32_t)src[i]) << 16;
        int remain = (int)(input_len - i);   /* 本组剩余可用字节数 1..3 */

        if (remain >= 2) {
            v |= ((uint32_t)src[i + 1]) << 8;
        }
        if (remain >= 3) {
            v |= (uint32_t)src[i + 2];
        }

        /* 每 6 bit 取一个字母表下标，共输出 4 个字符 */
        output[out++] = b64_alphabet[(v >> 18) & 0x3F];
        output[out++] = b64_alphabet[(v >> 12) & 0x3F];
        /* 不足 2 / 3 字节的位置用 '=' 占位 */
        output[out++] = (remain >= 2) ? b64_alphabet[(v >> 6) & 0x3F] : '=';
        output[out++] = (remain >= 3) ? b64_alphabet[v & 0x3F]        : '=';
    }

    output[out] = '\0';
    if (written != NULL) {
        *written = out;
    }
    return 0;
}

/*
 * Base64 解码。
 * 参数：
 *   input       - Base64 文本，允许出现 '\r'/'\n' 折行（会被跳过）
 *   input_len   - 输入字符数（不是 4 的倍数也接受）
 *   output      - 输出二进制缓冲
 *   output_size - 输出缓冲容量（字节）
 *   written     - 可选（可为 NULL），返回实际解码字节数
 * 返回：0 成功；-1 参数非法、含非法字符或输出空间不足
 * 注意：一旦遇到 '=' 就视为数据结束并停止解析（其后的内容被忽略），
 *       因此尾部填充不会被计入输出；本函数不校验填充位的规范性。
 */
int base64_decode(const char *input, size_t input_len,
                      void *output, size_t output_size,
                      size_t *written)
{
    uint8_t *dst = (uint8_t *)output;
    size_t i, out = 0;
    uint32_t acc = 0;   /* 位累加器：保存尚未满 8 bit 的残余比特 */
    int nbits = 0;      /* acc 中当前有效的比特数，范围 0..7 */

    if (input == NULL || output == NULL) {
        return -1;
    }

    for (i = 0; i < input_len; i++) {
        char c = input[i];
        int v;

        if (c == '=' || c == '\n' || c == '\r') {
            if (c == '=') {
                /* 忽略填充，循环结束后统一收尾 */
                break;
            }
            continue;   /* 换行符直接跳过，兼容折行格式 */
        }
        v = b64_value(c);
        if (v < 0) {
            return -1;  /* 非法字符立即失败 */
        }
        acc = (acc << 6) | (uint32_t)v;
        nbits += 6;
        /* 每凑满 8 bit 就输出一个字节，并把剩余比特留在 acc 低位 */
        if (nbits >= 8) {
            nbits -= 8;
            if (out >= output_size) {
                return -1;
            }
            dst[out++] = (uint8_t)((acc >> nbits) & 0xFFu);
        }
    }

    if (written != NULL) {
        *written = out;
    }
    return 0;
}
