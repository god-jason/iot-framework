#ifndef FKV_INTERNAL_H
#define FKV_INTERNAL_H

/*
 * fkv_internal.h - fkv 内部落盘格式与模块间接口
 * ============================================================================
 *
 * 只被本组件内部 .c 使用。所有落盘字段紧凑排列（无填充），小端序。
 */

#include "fkv.h"

#include <stdbool.h>
#include <string.h>

/* ============================== 物理常量 ============================== */

/*
 * 魔术字统一用 4 个可打印字符拼装（小端序），
 * 用编程器/十六进制查看器能直接认出分区与记录边界：
 *   FKV_SUPER_MAGIC  存储为 "FKVS"  —— 分区已提交
 *   FKV_REC_MAGIC    存储为 "FKVR"  —— 记录头
 *   FKV_COMMIT_MAGIC 存储为 "FKVC"  —— 记录提交标记（最后写入）
 */
#define FKV_MAGIC4(a, b, c, d) \
    ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | \
     ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

#define FKV_SUPER_MAGIC         FKV_MAGIC4('F', 'K', 'V', 'S')
#define FKV_REC_MAGIC           FKV_MAGIC4('F', 'K', 'V', 'R')
#define FKV_COMMIT_MAGIC        FKV_MAGIC4('F', 'K', 'V', 'C')
/* 擦除态 */
#define FKV_ERASED_U32          0xFFFFFFFFu

#define FKV_SUPER_SIZE          32u
#define FKV_REC_HDR_SIZE        24u
#define FKV_COMMIT_SIZE         4u

/* 记录标志：bit0 = 墓碑（删除该键） */
#define FKV_REC_FLAG_DELETED    0x01u

/* ============================== 落盘结构 ============================== */

/*
 * 分区超级块（32B，位于分区起始，最后写入）。
 * 只有 CRC 正确且 magic/version/hdr_size 合法时该分区才视为“已提交”。
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;             /* FKV_SUPER_MAGIC */
    uint16_t version;           /* FKV_VERSION */
    uint16_t hdr_size;          /* FKV_SUPER_SIZE */
    uint32_t generation;        /* 分区代次，越大越新 */
    uint32_t data_start;        /* 记录区起始偏移（= FKV_SUPER_SIZE） */
    uint32_t record_count;      /* 提交时已有记录数 */
    uint32_t live_bytes;        /* 有效 value 字节数（压缩时统计，诊断用） */
    uint32_t rsv;               /* 保留，写 0 */
    uint32_t crc32;             /* 前 28B 的 CRC32 */
} fkv_super_t;

/* 记录头（24B） */
typedef struct __attribute__((packed)) {
    uint32_t magic;             /* FKV_REC_MAGIC */
    uint16_t version;           /* FKV_VERSION */
    uint16_t length;            /* Payload 字节数 */
    uint32_t sequence;          /* 全局递增序号，越大越新 */
    uint32_t crc32;             /* Header(本字段按 0) + Payload 的 CRC32 */
    uint32_t key;               /* 键 */
    uint8_t  flags;             /* FKV_REC_FLAG_* */
    uint8_t  rsv[3];
} fkv_rec_hdr_t;

/* 扫描回调：返回非 0 可中止扫描，其值原样返回 */
typedef int (*fkv_scan_cb_t)(fkv_t *fs, const fkv_rec_hdr_t *hdr,
                             uint32_t rec_addr, void *ctx);

/* 键槽位：压缩与遍历时用于判定“该键的最新记录” */
typedef struct {
    uint32_t key;
    uint32_t seq;
    uint32_t addr;
    uint32_t len;
    uint8_t  alive;
    uint8_t  used;
} fkv_slot_t;

/* ============================== CRC32 ============================== */

#include "crc32.h"

/* ============================== 记录尺寸 ============================== */

static inline uint32_t fkv_align_up(uint32_t value, uint32_t align)
{
    return ((value + align - 1u) / align) * align;
}

static inline uint32_t fkv_prog_size(const fkv_t *fs)
{
    return (fs->dev.program_size < 4u) ? 4u : fs->dev.program_size;
}

/* Commit 在记录内的偏移 */
static inline uint32_t fkv_commit_off(const fkv_t *fs, uint32_t payload)
{
    return FKV_REC_HDR_SIZE + fkv_align_up(payload, fkv_prog_size(fs));
}

/* 整条记录占用（含尾对齐） */
static inline uint32_t fkv_rec_size(const fkv_t *fs, uint32_t payload)
{
    return fkv_align_up(fkv_commit_off(fs, payload) + FKV_COMMIT_SIZE,
                         fkv_prog_size(fs));
}

/* ============================== 块设备封装 ============================== */

int fkv_dev_read(fkv_t *fs, uint32_t addr, void *buf, uint32_t size);
int fkv_dev_program(fkv_t *fs, uint32_t addr, const void *buf, uint32_t size);
int fkv_dev_erase(fkv_t *fs, uint32_t addr, uint32_t size);

static inline uint32_t fkv_region_addr(const fkv_t *fs, uint32_t index)
{
    return fs->region_base[index & 1u];
}

/* ============================== 记录读写 ============================== */

/*
 * 在指定分区的 *off 处追加一条记录。
 * 成功后 *off 前进到记录末尾；seq 为写入序号（压缩拷贝时沿用原序号）。
 */
int fkv_rec_write(fkv_t *fs, uint32_t base, uint32_t *off, uint32_t key,
                   uint8_t flags, const void *payload, uint32_t length,
                   uint32_t seq);

/* 读取记录 Payload 的 [off, off+len) 区间 */
int fkv_rec_read(fkv_t *fs, uint32_t rec_addr, const fkv_rec_hdr_t *hdr,
                  uint32_t off, void *buf, uint32_t len);

/* 读取记录头；返回 FKV_OK / FKV_ERR_IO / FKV_ERR_CORRUPT（头部不合理） */
int fkv_hdr_read(fkv_t *fs, uint32_t addr, fkv_rec_hdr_t *hdr);

/* 记录头字段是否合理（magic/version/flags/长度） */
int fkv_hdr_sane(const fkv_t *fs, const fkv_rec_hdr_t *hdr);

/* 记录是否已提交（只读 Commit 4B，不校验 CRC） */
int fkv_rec_committed(fkv_t *fs, uint32_t rec_addr, const fkv_rec_hdr_t *hdr);

/* 记录是否已提交且 CRC 正确 */
int fkv_rec_valid(fkv_t *fs, uint32_t rec_addr, const fkv_rec_hdr_t *hdr);

/*
 * 扫描活动分区的记录日志。
 * 只返回结构合法且已提交的记录；遇到空尾/损坏尾部即停止。
 */
int fkv_scan(fkv_t *fs, fkv_scan_cb_t cb, void *ctx);

/* 追加一条记录到活动区（必要时触发分区滚动），并更新实例状态 */
int fkv_append(fkv_t *fs, uint32_t key, uint8_t flags,
                const void *payload, uint32_t length);

/* 确保活动区还能放下 need 字节；不足时滚动到另一分区 */
int fkv_ensure_space(fkv_t *fs, uint32_t need);

/* ============================== 分区与压缩 ============================== */

int fkv_super_read(fkv_t *fs, uint32_t index, fkv_super_t *sb);
int fkv_super_write(fkv_t *fs, uint32_t index, const fkv_super_t *sb);

/*
 * 扫描指定分区，输出可追加偏移、记录数、最大序号。
 * verify_crc 非 0 时逐条校验 CRC（挂载时为 0，fkv_check 时为 1）。
 */
int fkv_region_scan(fkv_t *fs, uint32_t index, uint8_t verify_crc,
                     uint32_t *write_off, uint32_t *count, uint32_t *max_seq);

/* 把活动区的有效记录搬到另一分区并提交（SUPER 最后写），随后擦除旧区 */
int fkv_compact(fkv_t *fs);

/*
 * 建立「每个键的最新记录」槽位表（静态表，调用者需保证不并发访问）。
 * 返回存活键数量；键数量超过 FKV_MAX_KEYS 返回 FKV_ERR_NOSPC。
 */
int fkv_build_latest(fkv_t *fs, uint32_t *live_count);

#endif /* FKV_INTERNAL_H */
