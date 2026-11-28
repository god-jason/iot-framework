#ifndef ROFS_INTERNAL_H
#define ROFS_INTERNAL_H

/*
 * rofs_internal.h - ROFS 内部格式与模块间接口
 * ============================================================================
 *
 * 只在组件内部使用。所有落盘字段统一按小端逐字节解析，避免结构体对齐
 * 与主机字节序差异。
 */

#include "rofs.h"

#include <string.h>

/* Super Header 字段偏移 */
#define ROFS_SB_OFF_MAGIC       0u
#define ROFS_SB_OFF_VERSION     4u
#define ROFS_SB_OFF_HDR_SIZE    6u
#define ROFS_SB_OFF_IMAGE_SIZE  8u
#define ROFS_SB_OFF_REC_COUNT   12u
#define ROFS_SB_OFF_REC_OFFSET  16u
#define ROFS_SB_OFF_REC_SIZE    20u
#define ROFS_SB_OFF_IMAGE_CRC   24u
#define ROFS_SB_OFF_FLAGS       28u

/* Record Header 字段偏移 */
#define ROFS_REC_OFF_TYPE       0u
#define ROFS_REC_OFF_NAME_LEN   1u
#define ROFS_REC_OFF_FLAGS      2u
#define ROFS_REC_OFF_SIZE       4u
#define ROFS_REC_OFF_TIME       8u
#define ROFS_REC_OFF_CRC        12u

/* 校验时使用的分块大小，控制栈占用 */
#define ROFS_CRC_CHUNK          64u

/* 解析后的记录头 */
typedef struct {
    uint8_t type;
    uint8_t name_len;
    uint16_t flags;
    uint32_t size;
    uint32_t time;
    uint32_t crc;
} rofs_rec_t;

/* 小端读写辅助 */
static inline uint16_t rofs__get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t rofs__get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/*
 * CRC32 统一使用 src/crypto 的实现：
 *   crc = crc32_update(0xFFFFFFFFu, buf, len) ^ 0xFFFFFFFFu;
 * 与 zlib.crc32 的 CRC-32/ISO-HDLC 一致。
 */
#include "crc32.h"

/*
 * 读取镜像内的连续数据（offset 为镜像内偏移）。
 * 自动检查 [offset, offset+size) 是否越过 image_size。
 */
int rofs__read(rofs_t *fs, uint32_t offset, void *buf, uint32_t size);

/* 读取并解析记录头；调用者需保证 rec_off 在记录区内 */
int rofs__rec_read(rofs_t *fs, uint32_t rec_off, rofs_rec_t *rec);

/* 读取记录文件名到 buf，保证 '\0' 结尾；cap 至少 name_len+1 */
int rofs__rec_name(rofs_t *fs, uint32_t rec_off, const rofs_rec_t *rec,
                   char *buf, uint32_t cap);

/*
 * 读取记录头、校验边界并计算 Body 偏移。
 * 用于顺序扫描；rec/body_off 可选择性输出。
 */
int rofs__record_load(rofs_t *fs, uint32_t off, rofs_rec_t *rec,
                      uint32_t *body_off);

/*
 * 路径校验。
 * allow_root 非 0 时允许 "" 或 "/"（用于 opendir）。
 * 通过返回 ROFS_OK，否则 ROFS_ERR_INVALID。
 */
int rofs__path_check(const char *path, int allow_root);

/*
 * 目录路径归一化：去掉前导 '/'，根目录输出空串。
 * 失败返回 ROFS_ERR_INVALID。
 */
int rofs__path_dir(const char *path, char *out, uint32_t cap);

/*
 * 按相对路径查找记录。
 * 成功时返回记录头与 Body 偏移（镜像内偏移）。
 */
int rofs__lookup(rofs_t *fs, const char *path, rofs_rec_t *rec,
                 uint32_t *body_off);

/* 目录是否存在（根目录始终存在） */
int rofs__dir_exists(rofs_t *fs, const char *dir);

/* 记录区结束偏移（不含 END 标记） */
static inline uint32_t rofs__records_end(const rofs_t *fs)
{
    return fs->records_offset + fs->records_size;
}

#endif /* ROFS_INTERNAL_H */
