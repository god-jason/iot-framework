#ifndef DEFLATE_H
#define DEFLATE_H

/*
 * deflate.h - DEFLATE（RFC 1951）压缩/解压
 * ============================================================================
 *
 * 压缩：
 *   - LZ77（32KB 窗口，匹配 3..258 字节）；
 *   - 固定 Huffman 编码，整个输入写成一个 final 块；
 *   - 若固定编码结果不小于存储块（stored），自动改用存储块，保证不劣化；
 *   - 输出为 raw deflate（无 zlib/gzip 头与校验），需要完整性校验时外层
 *     叠加 CRC。
 *
 * 解压：
 *   - 完整支持 stored / fixed / dynamic 三种块，可解 zlib 等标准编码器产生
 *     的 raw deflate 数据；
 *   - 边解边校验：截断、非法码表、非法距离、输出越界都会返回错误。
 *
 * 资源与重入：
 *   - 压缩使用静态哈希表（默认 1KB，DEFLATE_HASH_BITS 可调），非线程安全；
 *   - 解压使用静态 Huffman 表（约 0.8KB），同样非重入；
 *   - 解码不需要额外工作缓冲，只在失败时丢弃已完成的部分输出；
 *   - 未使用该模块时，链接器（-ffunction-sections/-fdata-sections +
 *     --gc-sections）会回收其代码与静态表，不占固件空间。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 返回码 */
#define DEFLATE_OK           0
#define DEFLATE_ERR_PARAM   (-1)   /* 空指针或参数非法 */
#define DEFLATE_ERR_SPACE   (-2)   /* 输出缓冲不足 */
#define DEFLATE_ERR_DATA    (-3)   /* 压缩流损坏或截断 */

/* 压缩后最大可能字节数（stored / 固定 Huffman 取较大者，含余量） */
uint32_t deflate_bound(uint32_t src_len);

/*
 * 压缩 src[0..src_len) 到 dst，长度为 *dst_len。
 * 返回 DEFLATE_OK / DEFLATE_ERR_PARAM / DEFLATE_ERR_SPACE。
 */
int deflate_compress(const void *src, uint32_t src_len,
                     void *dst, uint32_t dst_cap, uint32_t *dst_len);

/*
 * 解压 raw deflate 数据 src[0..src_len) 到 dst，长度为 *dst_len。
 * 返回 DEFLATE_OK / DEFLATE_ERR_PARAM / DEFLATE_ERR_SPACE / DEFLATE_ERR_DATA。
 */
int inflate_decompress(const void *src, uint32_t src_len,
                       void *dst, uint32_t dst_cap, uint32_t *dst_len);

/* ============================ 流式解压 ============================ */

/* 流式回退窗口（必须 >= 流中最大匹配距离，DEFLATE 上限 32KB） */
#define DEFLATE_STREAM_WINDOW   32768U

/*
 * 流式解压上下文：结构较大（32KB 窗口），仅用于“边解压边写 Flash”等
 * 无法一次性提供完整输出缓冲的场景。调用方用静态/堆分配，勿放小栈。
 * 非重入（内部复用静态 Huffman 表）。
 */
typedef struct {
    const uint8_t *src;
    uint32_t src_len;
    uint32_t src_pos;
    uint32_t bitbuf;
    uint32_t bitcnt;
    uint32_t out_total;         /* 已输出总字节数（跨多次 run 累计） */
    uint32_t stored_left;       /* 当前存储块剩余字节 */
    uint32_t match_left;        /* 跨 run 未复制的匹配字节数 */
    uint32_t match_dist;        /* 当前匹配距离 */
    int state;                  /* 0=块头 1=Huffman 块 2=存储块 3=结束 */
    int final;
    uint8_t window[DEFLATE_STREAM_WINDOW];
} deflate_inflate_t;

/* 初始化流式解压；返回 DEFLATE_OK / DEFLATE_ERR_PARAM */
int deflate_inflate_begin(deflate_inflate_t *ctx,
                          const void *src, uint32_t src_len);

/*
 * 继续解压，最多输出 cap 字节到 dst：
 *   produced 本次输出字节数；done=1 表示流已结束。
 * 每次调用前把上次 produced 写入的数据刷出即可（如写 Flash）。
 * 返回 DEFLATE_OK / DEFLATE_ERR_PARAM / DEFLATE_ERR_DATA
 * （dst 容量由调用方保证，本接口不返回 SPACE）。
 */
int deflate_inflate_run(deflate_inflate_t *ctx, void *dst, uint32_t cap,
                        uint32_t *produced, int *done);

#ifdef __cplusplus
}
#endif

#endif /* DEFLATE_H */
