/*
 * deflate.c - DEFLATE（RFC 1951）压缩/解压实现
 *
 * 压缩：LZ77 匹配 + 固定 Huffman（单块，BTYPE=01）；若结果不小于存储块，
 * 则改用 stored 块（BTYPE=00）。
 * 解压：完整解析 stored / fixed / dynamic 块，Huffman 解码采用规范的
 * count/symbol 表（按位逐层下降），无需额外工作缓冲。
 *
 * 位序：DEFLATE 的比特按低位先写/先读；Huffman 码本身是高位在前，
 * 编码时需先反转，解码时逐位累加。
 */

#include "deflate.h"

#include <string.h>

#define DEFLATE_WINDOW       32768U   /* LZ77 窗口 */
#define DEFLATE_MIN_MATCH    3U
#define DEFLATE_MAX_MATCH    258U
#define DEFLATE_MAX_BITS     15U
#define DEFLATE_STORED_MAX   65535U

/* 编码哈希表位数（默认 256 项，1KB）；越大压缩率略好、内存更多 */
#ifndef DEFLATE_HASH_BITS
#define DEFLATE_HASH_BITS    8U
#endif
#if (DEFLATE_HASH_BITS < 1) || (DEFLATE_HASH_BITS > 16)
#error "DEFLATE_HASH_BITS must be 1..16"
#endif
#define DEFLATE_HASH_SIZE    (1U << DEFLATE_HASH_BITS)

/* ---------------- 公共表 ---------------- */

static const uint16_t s_len_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};

static const uint8_t s_len_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};

static const uint16_t s_dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193,
    12289, 16385, 24577
};

static const uint8_t s_dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

/* 动态块中码长码表的发送顺序 */
static const uint8_t s_cl_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

/* ============================================================================
 * 压缩：位写入 + 固定 Huffman + LZ77
 * ==========================================================================*/

/* 位置 + 1，0 表示空；编码非重入 */
static uint32_t s_enc_hash[DEFLATE_HASH_SIZE];

typedef struct {
    uint8_t *dst;
    uint32_t cap;
    uint32_t pos;
    uint32_t buf;      /* 待写出比特（低位有效） */
    uint32_t cnt;      /* 待写出比特数 */
    int error;         /* 1 = 输出缓冲不足 */
} deflate_bw_t;

static void bw_put(deflate_bw_t *w, uint32_t value, uint32_t n)
{
    if (w->error != 0 || n == 0U) {
        return;
    }
    w->buf |= (value & ((1U << n) - 1U)) << w->cnt;
    w->cnt += n;
    while (w->cnt >= 8U) {
        if (w->pos >= w->cap) {
            w->error = 1;
            return;
        }
        w->dst[w->pos++] = (uint8_t)(w->buf & 0xFFU);
        w->buf >>= 8;
        w->cnt -= 8U;
    }
}

/* 把剩余比特补 0 写到字节边界 */
static void bw_flush(deflate_bw_t *w)
{
    if (w->error == 0 && w->cnt != 0U) {
        if (w->pos >= w->cap) {
            w->error = 1;
            return;
        }
        w->dst[w->pos++] = (uint8_t)(w->buf & 0xFFU);
        w->buf = 0U;
        w->cnt = 0U;
    }
}

/* 反转低 n 位（Huffman 码高位在前，流中低位先写） */
static uint32_t bit_reverse(uint32_t v, uint32_t n)
{
    uint32_t r = 0U;

    while (n-- > 0U) {
        r = (r << 1) | (v & 1U);
        v >>= 1;
    }
    return r;
}

/* 固定 Huffman 字面量/长度码（RFC 1951 3.2.6） */
static void put_litlen(deflate_bw_t *w, uint32_t sym)
{
    uint32_t code;
    uint32_t len;

    if (sym <= 143U) {
        code = 0x30U + sym;
        len = 8U;
    } else if (sym <= 255U) {
        code = 0x190U + (sym - 144U);
        len = 9U;
    } else if (sym <= 279U) {
        code = sym - 256U;
        len = 7U;
    } else {
        code = 0xC0U + (sym - 280U);
        len = 8U;
    }
    bw_put(w, bit_reverse(code, len), len);
}

/* 匹配对：长度码 + 距离码（固定 Huffman 距离码为 5 位） */
static void put_match(deflate_bw_t *w, uint32_t len, uint32_t dist)
{
    uint32_t idx = 0U;

    while (idx < 28U && len >= s_len_base[idx + 1U]) {
        idx++;
    }
    put_litlen(w, 257U + idx);
    if (s_len_extra[idx] != 0U) {
        bw_put(w, len - s_len_base[idx], s_len_extra[idx]);
    }

    idx = 0U;
    while (idx < 29U && dist >= s_dist_base[idx + 1U]) {
        idx++;
    }
    bw_put(w, bit_reverse(idx, 5U), 5U);
    if (s_dist_extra[idx] != 0U) {
        bw_put(w, dist - s_dist_base[idx], s_dist_extra[idx]);
    }
}

static uint32_t def_hash(const uint8_t *p)
{
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                 ((uint32_t)p[2] << 16);

    return (v * 2654435761U) >> (32U - DEFLATE_HASH_BITS);
}

/* 固定 Huffman 编码整个输入；返回输出长度，*error 置 1 表示空间不足 */
static uint32_t deflate_fixed(const uint8_t *s, uint32_t n,
                              uint8_t *d, uint32_t cap, int *error)
{
    deflate_bw_t w;
    uint32_t ip = 0U;

    w.dst = d;
    w.cap = cap;
    w.pos = 0U;
    w.buf = 0U;
    w.cnt = 0U;
    w.error = 0;

    memset(s_enc_hash, 0, sizeof(s_enc_hash));

    /* 块头：BFINAL=1，BTYPE=01（先写低 1 位 final，再写 2 位 type） */
    bw_put(&w, 1U, 1U);
    bw_put(&w, 1U, 2U);

    while (ip < n && w.error == 0) {
        uint32_t best_len = 0U;
        uint32_t best_dist = 0U;

        if ((ip + 2U) < n) {            /* 至少 3 字节才能建哈希 */
            uint32_t h = def_hash(&s[ip]);
            uint32_t ref = s_enc_hash[h];

            s_enc_hash[h] = ip + 1U;
            if (ref != 0U) {
                uint32_t cand = ref - 1U;
                uint32_t dist = ip - cand;

                if (dist >= 1U && dist <= DEFLATE_WINDOW) {
                    uint32_t max_len = n - ip;
                    uint32_t len = 0U;

                    if (max_len > DEFLATE_MAX_MATCH) {
                        max_len = DEFLATE_MAX_MATCH;
                    }
                    while (len < max_len && s[cand + len] == s[ip + len]) {
                        len++;
                    }
                    if (len >= DEFLATE_MIN_MATCH) {
                        best_len = len;
                        best_dist = dist;
                    }
                }
            }
        }

        if (best_len >= DEFLATE_MIN_MATCH) {
            uint32_t k;

            put_match(&w, best_len, best_dist);
            for (k = 1U; k < best_len && (ip + k + 2U) < n; k++) {
                s_enc_hash[def_hash(&s[ip + k])] = ip + k + 1U;
            }
            ip += best_len;
        } else {
            put_litlen(&w, s[ip]);
            ip++;
        }
    }

    put_litlen(&w, 256U);               /* 块结束 */
    bw_flush(&w);

    *error = w.error;
    return w.pos;
}

/* 存储块编码（每块最多 65535 字节，字节对齐）；返回长度，0 表示空间不足 */
static uint32_t deflate_stored(const uint8_t *s, uint32_t n,
                               uint8_t *d, uint32_t cap)
{
    uint32_t ip = 0U;
    uint32_t op = 0U;

    if (n == 0U) {
        if (cap < 5U) {
            return 0U;
        }
        d[op++] = 0x01U;                /* BFINAL=1, BTYPE=00 */
        d[op++] = 0x00U;
        d[op++] = 0x00U;
        d[op++] = 0xFFU;
        d[op++] = 0xFFU;
        return op;
    }

    while (ip < n) {
        uint32_t blk = n - ip;
        uint32_t i;

        if (blk > DEFLATE_STORED_MAX) {
            blk = DEFLATE_STORED_MAX;
        }
        if ((op + 5U + blk) > cap) {
            return 0U;
        }
        d[op++] = (ip + blk >= n) ? 0x01U : 0x00U;   /* BFINAL + BTYPE=00 */
        d[op++] = (uint8_t)(blk & 0xFFU);
        d[op++] = (uint8_t)((blk >> 8) & 0xFFU);
        i = ~blk;
        d[op++] = (uint8_t)(i & 0xFFU);
        d[op++] = (uint8_t)((i >> 8) & 0xFFU);
        memcpy(&d[op], &s[ip], blk);
        op += blk;
        ip += blk;
    }
    return op;
}

/* 存储块编码后的总字节数 */
static uint32_t deflate_stored_size(uint32_t n)
{
    uint64_t blocks;

    if (n == 0U) {
        return 5U;
    }
    blocks = ((uint64_t)n + DEFLATE_STORED_MAX - 1U) / DEFLATE_STORED_MAX;
    return (uint32_t)((uint64_t)n + 5U * blocks);
}

uint32_t deflate_bound(uint32_t src_len)
{
    uint64_t n = (uint64_t)src_len;
    uint64_t blocks = (n + DEFLATE_STORED_MAX - 1U) / DEFLATE_STORED_MAX;
    uint64_t stored = n + 5U * blocks;
    uint64_t fixed = ((n * 9U) + 7U) / 8U + 2U;
    uint64_t bound = (stored > fixed) ? stored : fixed;

    bound += 8U;                        /* 余量 */
    return (bound > 0xFFFFFFFFULL) ? 0xFFFFFFFFU : (uint32_t)bound;
}

int deflate_compress(const void *src, uint32_t src_len,
                     void *dst, uint32_t dst_cap, uint32_t *dst_len)
{
    const uint8_t *s = (const uint8_t *)src;
    uint8_t *d = (uint8_t *)dst;
    uint32_t fixed_len;
    uint32_t out;
    int err = 0;

    if ((src == NULL && src_len != 0U) || dst == NULL || dst_len == NULL) {
        return DEFLATE_ERR_PARAM;
    }

    fixed_len = deflate_fixed(s, src_len, d, dst_cap, &err);
    if (err != 0) {
        out = deflate_stored(s, src_len, d, dst_cap);
        if (out == 0U) {
            return DEFLATE_ERR_SPACE;
        }
        *dst_len = out;
        return DEFLATE_OK;
    }

    if (deflate_stored_size(src_len) < fixed_len) {
        out = deflate_stored(s, src_len, d, dst_cap);
        if (out != 0U) {
            *dst_len = out;
            return DEFLATE_OK;
        }
    }

    *dst_len = fixed_len;
    return DEFLATE_OK;
}

/* ============================================================================
 * 解压：位读取 + Huffman 解码
 * ==========================================================================*/

typedef struct {
    const uint8_t *p;
    uint32_t len;
    uint32_t pos;
    uint32_t buf;
    uint32_t cnt;
} deflate_br_t;

/* 读取 n(<=16) 位，低位先出 */
static int br_get(deflate_br_t *b, uint32_t n, uint32_t *out)
{
    while (b->cnt < n) {
        if (b->pos >= b->len) {
            return DEFLATE_ERR_DATA;
        }
        b->buf |= (uint32_t)b->p[b->pos++] << b->cnt;
        b->cnt += 8U;
    }
    *out = b->buf & ((1U << n) - 1U);
    b->buf >>= n;
    b->cnt -= n;
    return DEFLATE_OK;
}

/* Huffman 表：码数统计 + 按码长排序的符号表（符号表大小按用途裁剪） */
typedef struct {
    uint16_t count[DEFLATE_MAX_BITS + 1];
    uint16_t symbol[288];               /* 字面量/长度码 0..287 */
} deflate_lit_huff_t;

typedef struct {
    uint16_t count[DEFLATE_MAX_BITS + 1];
    uint16_t symbol[32];                /* 距离码 0..31 */
} deflate_dist_huff_t;

typedef struct {
    uint16_t count[DEFLATE_MAX_BITS + 1];
    uint16_t symbol[19];                /* 码长码 0..18 */
} deflate_cl_huff_t;

/*
 * 静态 Huffman 表：解压非重入。
 * s_lit / s_dist 在每次遇到 fixed 或 dynamic 块时重建（两者共用，
 * 同一数据流里可能混合出现）。
 */
static deflate_lit_huff_t s_lit;
static deflate_dist_huff_t s_dist;
static deflate_cl_huff_t s_cl;

/*
 * 用码长表构建规范 Huffman：
 *   count[len] 为该长度的码数，symbol[] 按码长升序、同长度按符号升序排列。
 * 返回 DEFLATE_OK；过完备码表返回 DEFLATE_ERR_DATA。
 */
static int huff_build(uint16_t *count, uint16_t *symbol,
                      const uint8_t *lengths, uint32_t n)
{
    uint16_t offs[DEFLATE_MAX_BITS + 1];
    int32_t left = 1;
    uint32_t i;

    memset(count, 0, sizeof(uint16_t) * (DEFLATE_MAX_BITS + 1U));
    for (i = 0U; i < n; i++) {
        count[lengths[i]]++;
    }
    if (count[0] == n) {
        return DEFLATE_OK;              /* 空树（例如无匹配的距离码） */
    }
    count[0] = 0U;
    for (i = 1U; i <= DEFLATE_MAX_BITS; i++) {
        left <<= 1;
        left -= count[i];
        if (left < 0) {
            return DEFLATE_ERR_DATA;    /* 过完备 */
        }
    }

    offs[1] = 0U;
    for (i = 1U; i < DEFLATE_MAX_BITS; i++) {
        offs[i + 1U] = (uint16_t)(offs[i] + count[i]);
    }
    for (i = 0U; i < n; i++) {
        if (lengths[i] != 0U) {
            symbol[offs[lengths[i]]++] = (uint16_t)i;
        }
    }
    return DEFLATE_OK;
}

/* 解码一个 Huffman 符号 */
static int huff_decode(deflate_br_t *b, const uint16_t *count,
                       const uint16_t *symbol, uint32_t *sym)
{
    int32_t code = 0;
    int32_t first = 0;
    int32_t index = 0;
    uint32_t len;

    for (len = 1U; len <= DEFLATE_MAX_BITS; len++) {
        uint32_t bit;
        int32_t cnt;

        if (br_get(b, 1U, &bit) != DEFLATE_OK) {
            return DEFLATE_ERR_DATA;
        }
        code |= (int32_t)bit;
        cnt = (int32_t)count[len];
        if ((code - cnt) < first) {
            *sym = symbol[index + (code - first)];
            return DEFLATE_OK;
        }
        index += cnt;
        first += cnt;
        first <<= 1;
        code <<= 1;
    }
    return DEFLATE_ERR_DATA;            /* 无匹配码 */
}

/* 构建固定 Huffman 表（RFC 1951 3.2.6）；与动态表共用，每个 fixed 块重建 */
static void fixed_tables(void)
{
    uint8_t lens[288];
    uint32_t i;

    for (i = 0U; i <= 143U; i++) {
        lens[i] = 8U;
    }
    for (; i <= 255U; i++) {
        lens[i] = 9U;
    }
    for (; i <= 279U; i++) {
        lens[i] = 7U;
    }
    for (; i <= 287U; i++) {
        lens[i] = 8U;
    }
    (void)huff_build(s_lit.count, s_lit.symbol, lens, 288U);
    for (i = 0U; i < 32U; i++) {
        lens[i] = 5U;
    }
    (void)huff_build(s_dist.count, s_dist.symbol, lens, 32U);
}

/* 解压一个 Huffman 块（s_lit / s_dist 已就绪），直到块结束符 */
static int inflate_block(deflate_br_t *b, uint8_t *dst, uint32_t cap,
                         uint32_t *op_io)
{
    uint32_t op = *op_io;

    for (;;) {
        uint32_t sym;
        uint32_t len;
        uint32_t dist_sym;
        uint32_t dist;
        uint32_t extra;
        uint32_t i;

        if (huff_decode(b, s_lit.count, s_lit.symbol, &sym) != DEFLATE_OK) {
            return DEFLATE_ERR_DATA;
        }
        if (sym < 256U) {
            if (op >= cap) {
                return DEFLATE_ERR_SPACE;
            }
            dst[op++] = (uint8_t)sym;
            continue;
        }
        if (sym == 256U) {
            break;                      /* 块结束 */
        }

        sym -= 257U;
        if (sym >= 29U) {
            return DEFLATE_ERR_DATA;
        }
        len = s_len_base[sym];
        if (s_len_extra[sym] != 0U) {
            if (br_get(b, s_len_extra[sym], &extra) != DEFLATE_OK) {
                return DEFLATE_ERR_DATA;
            }
            len += extra;
        }

        if (huff_decode(b, s_dist.count, s_dist.symbol, &dist_sym) !=
            DEFLATE_OK) {
            return DEFLATE_ERR_DATA;
        }
        if (dist_sym >= 30U) {
            return DEFLATE_ERR_DATA;
        }
        dist = s_dist_base[dist_sym];
        if (s_dist_extra[dist_sym] != 0U) {
            if (br_get(b, s_dist_extra[dist_sym], &extra) != DEFLATE_OK) {
                return DEFLATE_ERR_DATA;
            }
            dist += extra;
        }

        if (dist > op) {
            return DEFLATE_ERR_DATA;    /* 引用尚未输出的数据 */
        }
        if ((op + len) > cap) {
            return DEFLATE_ERR_SPACE;
        }
        for (i = 0U; i < len; i++) {
            dst[op] = dst[op - dist];   /* 支持 dist < len 重叠 */
            op++;
        }
    }

    *op_io = op;
    return DEFLATE_OK;
}

/* 读取动态块的两张码表 */
static int inflate_read_dynamic(deflate_br_t *b)
{
    uint8_t cl_lens[19];
    uint8_t lengths[286U + 32U];
    uint32_t hlit;
    uint32_t hdist;
    uint32_t hclen;
    uint32_t v;
    uint32_t i;

    if (br_get(b, 5U, &v) != DEFLATE_OK) {
        return DEFLATE_ERR_DATA;
    }
    hlit = v + 257U;
    if (br_get(b, 5U, &v) != DEFLATE_OK) {
        return DEFLATE_ERR_DATA;
    }
    hdist = v + 1U;
    if (br_get(b, 4U, &v) != DEFLATE_OK) {
        return DEFLATE_ERR_DATA;
    }
    hclen = v + 4U;
    if (hlit > 286U || hdist > 32U) {
        return DEFLATE_ERR_DATA;
    }

    memset(cl_lens, 0, sizeof(cl_lens));
    for (i = 0U; i < hclen; i++) {
        if (br_get(b, 3U, &v) != DEFLATE_OK) {
            return DEFLATE_ERR_DATA;
        }
        cl_lens[s_cl_order[i]] = (uint8_t)v;
    }
    if (huff_build(s_cl.count, s_cl.symbol, cl_lens, 19U) != DEFLATE_OK) {
        return DEFLATE_ERR_DATA;
    }

    memset(lengths, 0, sizeof(lengths));
    i = 0U;
    while (i < (hlit + hdist)) {
        uint32_t sym;
        uint32_t rep;
        uint8_t val;

        if (huff_decode(b, s_cl.count, s_cl.symbol, &sym) != DEFLATE_OK) {
            return DEFLATE_ERR_DATA;
        }
        if (sym < 16U) {
            lengths[i++] = (uint8_t)sym;
            continue;
        }
        if (sym == 16U) {
            if (i == 0U) {
                return DEFLATE_ERR_DATA;    /* 前面没有可复制的码长 */
            }
            val = lengths[i - 1U];
            if (br_get(b, 2U, &rep) != DEFLATE_OK) {
                return DEFLATE_ERR_DATA;
            }
            rep += 3U;
        } else if (sym == 17U) {
            val = 0U;
            if (br_get(b, 3U, &rep) != DEFLATE_OK) {
                return DEFLATE_ERR_DATA;
            }
            rep += 3U;
        } else if (sym == 18U) {
            val = 0U;
            if (br_get(b, 7U, &rep) != DEFLATE_OK) {
                return DEFLATE_ERR_DATA;
            }
            rep += 11U;
        } else {
            return DEFLATE_ERR_DATA;
        }
        if ((i + rep) > (hlit + hdist)) {
            return DEFLATE_ERR_DATA;
        }
        while (rep-- > 0U) {
            lengths[i++] = val;
        }
    }

    if (huff_build(s_lit.count, s_lit.symbol, lengths, hlit) != DEFLATE_OK) {
        return DEFLATE_ERR_DATA;
    }
    if (huff_build(s_dist.count, s_dist.symbol, lengths + hlit, hdist) !=
        DEFLATE_OK) {
        return DEFLATE_ERR_DATA;
    }
    return DEFLATE_OK;
}

int inflate_decompress(const void *src, uint32_t src_len,
                       void *dst, uint32_t dst_cap, uint32_t *dst_len)
{
    deflate_br_t br;
    uint8_t *d = (uint8_t *)dst;
    uint32_t op = 0U;
    int final;

    if ((src == NULL && src_len != 0U) || (dst == NULL && dst_cap != 0U) ||
        dst_len == NULL) {
        return DEFLATE_ERR_PARAM;
    }

    br.p = (const uint8_t *)src;
    br.len = src_len;
    br.pos = 0U;
    br.buf = 0U;
    br.cnt = 0U;

    do {
        uint32_t v;
        uint32_t type;
        int rc;

        if (br_get(&br, 1U, &v) != DEFLATE_OK) {
            return DEFLATE_ERR_DATA;
        }
        final = (int)v;
        if (br_get(&br, 2U, &v) != DEFLATE_OK) {
            return DEFLATE_ERR_DATA;
        }
        type = v;

        if (type == 0U) {
            uint32_t len;
            uint32_t nlen;

            /* 丢弃剩余比特，对齐到字节 */
            br.buf = 0U;
            br.cnt = 0U;
            if ((br.pos + 4U) > br.len) {
                return DEFLATE_ERR_DATA;
            }
            len = (uint32_t)br.p[br.pos] |
                  ((uint32_t)br.p[br.pos + 1U] << 8);
            nlen = (uint32_t)br.p[br.pos + 2U] |
                   ((uint32_t)br.p[br.pos + 3U] << 8);
            br.pos += 4U;
            if ((len ^ 0xFFFFU) != nlen) {
                return DEFLATE_ERR_DATA;
            }
            if ((br.pos + len) > br.len) {
                return DEFLATE_ERR_DATA;
            }
            if ((op + len) > dst_cap) {
                return DEFLATE_ERR_SPACE;
            }
            memcpy(&d[op], &br.p[br.pos], len);
            br.pos += len;
            op += len;
        } else if (type == 1U) {
            fixed_tables();
            rc = inflate_block(&br, d, dst_cap, &op);
            if (rc != DEFLATE_OK) {
                return rc;
            }
        } else if (type == 2U) {
            rc = inflate_read_dynamic(&br);
            if (rc != DEFLATE_OK) {
                return rc;
            }
            rc = inflate_block(&br, d, dst_cap, &op);
            if (rc != DEFLATE_OK) {
                return rc;
            }
        } else {
            return DEFLATE_ERR_DATA;    /* BTYPE=11 保留 */
        }
    } while (final == 0);

    *dst_len = op;
    return DEFLATE_OK;
}

/* ============================================================================
 * 流式解压：输出分多批产出（用于边解压边写 Flash）
 * ==========================================================================*/

int deflate_inflate_begin(deflate_inflate_t *ctx, const void *src,
                          uint32_t src_len)
{
    if (ctx == NULL || (src == NULL && src_len != 0U)) {
        return DEFLATE_ERR_PARAM;
    }
    ctx->src = (const uint8_t *)src;
    ctx->src_len = src_len;
    ctx->src_pos = 0U;
    ctx->bitbuf = 0U;
    ctx->bitcnt = 0U;
    ctx->out_total = 0U;
    ctx->stored_left = 0U;
    ctx->match_left = 0U;
    ctx->match_dist = 0U;
    ctx->state = 0;
    ctx->final = 0;
    return DEFLATE_OK;
}

/* 从流中读 n(<=16) 位 */
static int sbr_get(deflate_inflate_t *c, uint32_t n, uint32_t *out)
{
    while (c->bitcnt < n) {
        if (c->src_pos >= c->src_len) {
            return DEFLATE_ERR_DATA;
        }
        c->bitbuf |= (uint32_t)c->src[c->src_pos++] << c->bitcnt;
        c->bitcnt += 8U;
    }
    *out = c->bitbuf & ((1U << n) - 1U);
    c->bitbuf >>= n;
    c->bitcnt -= n;
    return DEFLATE_OK;
}

/* 借用共享 Huffman 解码（把读者状态临时映射到 deflate_br_t） */
static int sbr_huff(deflate_inflate_t *c, const uint16_t *count,
                    const uint16_t *symbol, uint32_t *sym)
{
    deflate_br_t br;
    int rc;

    br.p = c->src;
    br.len = c->src_len;
    br.pos = c->src_pos;
    br.buf = c->bitbuf;
    br.cnt = c->bitcnt;
    rc = huff_decode(&br, count, symbol, sym);
    c->src_pos = br.pos;
    c->bitbuf = br.buf;
    c->bitcnt = br.cnt;
    return rc;
}

/* 读取动态块码表（复用缓冲区版实现） */
static int sbr_dynamic(deflate_inflate_t *c)
{
    deflate_br_t br;
    int rc;

    br.p = c->src;
    br.len = c->src_len;
    br.pos = c->src_pos;
    br.buf = c->bitbuf;
    br.cnt = c->bitcnt;
    rc = inflate_read_dynamic(&br);
    c->src_pos = br.pos;
    c->bitbuf = br.buf;
    c->bitcnt = br.cnt;
    return rc;
}

/* 输出一个字节：写入 dst，同时记入 32KB 回退窗口 */
static void sbr_emit(deflate_inflate_t *c, uint8_t *out, uint32_t *op,
                     uint8_t b)
{
    out[*op] = b;
    (*op)++;
    c->window[c->out_total % DEFLATE_STREAM_WINDOW] = b;
    c->out_total++;
}

int deflate_inflate_run(deflate_inflate_t *ctx, void *dst, uint32_t cap,
                        uint32_t *produced, int *done)
{
    uint8_t *out = (uint8_t *)dst;
    uint32_t op = 0U;

    if (ctx == NULL || (dst == NULL && cap != 0U) || produced == NULL ||
        done == NULL) {
        return DEFLATE_ERR_PARAM;
    }

    *produced = 0U;
    *done = (ctx->state == 3) ? 1 : 0;
    if (*done != 0) {
        return DEFLATE_OK;
    }

    while (op < cap) {
        if (ctx->state == 3) {
            break;
        }

        if (ctx->state == 0) {
            uint32_t v;
            uint32_t type;

            if (sbr_get(ctx, 1U, &v) != DEFLATE_OK) {
                return DEFLATE_ERR_DATA;
            }
            ctx->final = (int)v;
            if (sbr_get(ctx, 2U, &v) != DEFLATE_OK) {
                return DEFLATE_ERR_DATA;
            }
            type = v;

            if (type == 0U) {
                uint32_t len;
                uint32_t nlen;

                ctx->bitbuf = 0U;       /* 对齐到字节 */
                ctx->bitcnt = 0U;
                if ((ctx->src_pos + 4U) > ctx->src_len) {
                    return DEFLATE_ERR_DATA;
                }
                len = (uint32_t)ctx->src[ctx->src_pos] |
                      ((uint32_t)ctx->src[ctx->src_pos + 1U] << 8);
                nlen = (uint32_t)ctx->src[ctx->src_pos + 2U] |
                       ((uint32_t)ctx->src[ctx->src_pos + 3U] << 8);
                ctx->src_pos += 4U;
                if ((len ^ 0xFFFFU) != nlen) {
                    return DEFLATE_ERR_DATA;
                }
                if ((ctx->src_pos + len) > ctx->src_len) {
                    return DEFLATE_ERR_DATA;
                }
                ctx->stored_left = len;
                ctx->state = 2;
            } else if (type == 1U) {
                fixed_tables();
                ctx->state = 1;
            } else if (type == 2U) {
                if (sbr_dynamic(ctx) != DEFLATE_OK) {
                    return DEFLATE_ERR_DATA;
                }
                ctx->state = 1;
            } else {
                return DEFLATE_ERR_DATA;
            }
            continue;
        }

        if (ctx->state == 2) {
            uint32_t n = ctx->stored_left;

            if (n > (cap - op)) {
                n = cap - op;
            }
            while (n-- > 0U) {
                sbr_emit(ctx, out, &op, ctx->src[ctx->src_pos++]);
                ctx->stored_left--;
            }
            if (ctx->stored_left == 0U) {
                ctx->state = (ctx->final != 0) ? 3 : 0;
            }
            continue;
        }

        /* state == 1：Huffman 块，先续完跨批次的匹配 */
        {
            int eob = 0;

            while (op < cap) {
                uint32_t sym;

                if (ctx->match_left > 0U) {
                    uint8_t b = ctx->window[(ctx->out_total -
                                             ctx->match_dist) %
                                            DEFLATE_STREAM_WINDOW];

                    sbr_emit(ctx, out, &op, b);
                    ctx->match_left--;
                    continue;
                }

                if (sbr_huff(ctx, s_lit.count, s_lit.symbol, &sym) !=
                    DEFLATE_OK) {
                    return DEFLATE_ERR_DATA;
                }
                if (sym < 256U) {
                    sbr_emit(ctx, out, &op, (uint8_t)sym);
                    continue;
                }
                if (sym == 256U) {
                    eob = 1;
                    break;
                }
                sym -= 257U;
                if (sym >= 29U) {
                    return DEFLATE_ERR_DATA;
                }
                {
                    uint32_t len = s_len_base[sym];
                    uint32_t dist_sym;
                    uint32_t dist;
                    uint32_t extra;

                    if (s_len_extra[sym] != 0U) {
                        if (sbr_get(ctx, s_len_extra[sym], &extra) !=
                            DEFLATE_OK) {
                            return DEFLATE_ERR_DATA;
                        }
                        len += extra;
                    }
                    if (sbr_huff(ctx, s_dist.count, s_dist.symbol,
                                 &dist_sym) != DEFLATE_OK) {
                        return DEFLATE_ERR_DATA;
                    }
                    if (dist_sym >= 30U) {
                        return DEFLATE_ERR_DATA;
                    }
                    dist = s_dist_base[dist_sym];
                    if (s_dist_extra[dist_sym] != 0U) {
                        if (sbr_get(ctx, s_dist_extra[dist_sym], &extra) !=
                            DEFLATE_OK) {
                            return DEFLATE_ERR_DATA;
                        }
                        dist += extra;
                    }
                    if (dist > ctx->out_total ||
                        dist > DEFLATE_STREAM_WINDOW) {
                        return DEFLATE_ERR_DATA;
                    }
                    ctx->match_left = len;
                    ctx->match_dist = dist;
                }
            }
            if (eob != 0) {
                ctx->state = (ctx->final != 0) ? 3 : 0;
            }
        }
    }

    *produced = op;
    if (ctx->state == 3) {
        *done = 1;
    }
    return DEFLATE_OK;
}
