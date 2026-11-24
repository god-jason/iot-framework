#ifndef MFS_INTERNAL_H
#define MFS_INTERNAL_H

/*
 * mfs_internal.h - MFS 内部物理格式与模块间接口
 * ============================================================================
 *
 * 本文件不对外公开，只被 MFS 组件内部 .c 使用。
 *
 * 物理存储三层概念：
 *   - Erase Block：最小擦除单元，由一个 Block Header + 多条 Record 组成；
 *   - Record     ：一次追加写入的完整日志，带 CRC 和提交标记；
 *   - Data Chunk ：文件逻辑数据块，固定 256B，与物理块不对应。
 *
 * 所有多字节字段一律使用小端序写入 Flash，避免不同 MCU 的内存对齐差异。
 */

#include "mfs.h"

#include <stdbool.h>
#include <string.h>

/* ============================================================================
 * 物理格式常量
 * ==========================================================================*/

/* Superblock 魔数："ROFS"（小端字节序 R O F S） */
#define MFS_SB_MAGIC         0x53464F52u
/* Block Header 魔数："!BLK" */
#define MFS_BLK_MAGIC        0x4B4C4221u
/* Record Header 魔数："REC1" */
#define MFS_REC_MAGIC        0x31434552u
/* Commit Marker 魔数："CMIT"，记录尾部最后写入 */
#define MFS_CMT_MAGIC        0x54494D43u

#define MFS_SB_SIZE          24u   /* Superblock 序列化长度 */
#define MFS_BLK_HDR_SIZE     24u   /* Block Header 长度，也是块内首条记录偏移 */
#define MFS_REC_HDR_SIZE     32u   /* Record Header 长度 */
#define MFS_DATA_PREFIX      24u   /* DATA 记录 Payload 前缀长度（含 4B 保留） */
#define MFS_INODE_PREFIX     32u   /* INODE 记录 Payload 固定前缀长度 */

/* Block Header state 字段取值 */
#define MFS_BLK_STATE_FREE   1u    /* 已擦除，可分配 */
#define MFS_BLK_STATE_BAD    2u    /* 坏块，永久跳过 */

/* active_block 的哨兵值：当前没有可追加块，需要时再分配 */
#define MFS_BLOCK_NONE       0xFFFFFFFFu

/* Inode 类型，与公共头文件 mfs_stat_t.type 一致 */
#define MFS_TYPE_FILE        1u
#define MFS_TYPE_DIR         2u

/*
 * Record 类型。
 * 目前实际使用：INODE_CREATE/UPDATE/DELETE、DATA、DATA_DELETE。
 * TX_COMMIT/CHECKPOINT/GC_MOVE 为后续版本预留。
 */
#define MFS_REC_INODE_CREATE 1u   /* 创建 Inode（文件或目录） */
#define MFS_REC_INODE_UPDATE 2u   /* 更新 Inode（大小/名称/父目录等） */
#define MFS_REC_INODE_DELETE 3u   /* 删除 Inode */
#define MFS_REC_DATA         4u   /* 文件数据 Chunk */
#define MFS_REC_DATA_DELETE  5u   /* 批量失效一段 Chunk（截断） */
#define MFS_REC_TX_COMMIT    6u   /* 事务提交（预留） */
#define MFS_REC_CHECKPOINT   7u   /* Checkpoint（预留） */
#define MFS_REC_GC_MOVE      8u   /* GC 搬迁（预留） */

/* ============================================================================
 * 内存中的格式结构
 * ==========================================================================*/

/*
 * Block Header：每个池内块开头 24B。
 * 块被擦除后写入一次，记录擦除次数与状态；之后不再原地修改。
 * 已分配块通过“是否存在有效 Record”来判定，不依赖改写 state。
 */
typedef struct {
    uint32_t magic;        /* MFS_BLK_MAGIC */
    uint32_t sequence;     /* 擦除/分配顺序，用于找出最新活动块 */
    uint32_t erase_count;  /* 累计擦除次数，用于磨损均衡 */
    uint32_t state;        /* MFS_BLK_STATE_FREE / MFS_BLK_STATE_BAD */
    uint32_t crc;          /* 前 16B 的 CRC32（不含本字段与 reserved） */
    uint32_t reserved;     /* 保留，写 0 */
} mfs_blk_hdr_t;

/*
 * Superblock：分区起始处 A/B 两份，每份占一个擦除块的前 24B。
 * A 与 B 内容相同，代次 generation 更大的一份优先。
 */
typedef struct {
    uint32_t magic;        /* MFS_SB_MAGIC */
    uint32_t version;      /* (MAJOR << 16) | MINOR */
    uint32_t block_size;   /* 块大小，必须与设备 erase_size 一致 */
    uint32_t block_count;  /* 数据池块数（不含 A/B 两个 Superblock 块） */
    uint32_t generation;   /* 格式化代次，每次 format 递增 */
    uint32_t crc;          /* 前 20B 的 CRC32 */
} mfs_sb_t;

/*
 * Record Header：每条日志记录的 32B 头部。
 * 序列化顺序与结构体字段顺序一致，全部小端。
 */
typedef struct {
    uint32_t magic;        /* MFS_REC_MAGIC */
    uint32_t sequence;     /* 全局单调递增序号 */
    uint32_t tx_id;        /* 事务编号，V1 每次操作相同，仅用于跟踪 */
    uint32_t object_id;    /* 关联对象：通常是 Inode 编号 */
    uint16_t type;         /* MFS_REC_* 类型 */
    uint16_t flags;        /* 保留 */
    uint32_t payload_size; /* Payload 字节数 */
    uint32_t payload_crc;  /* Payload 的 CRC32 */
    uint32_t header_crc;   /* 本头部前 28B 的 CRC32 */
} mfs_rec_hdr_t;

/*
 * Inode 运行时信息（内部）。
 * 对应 INODE Payload 解析结果，附带最新记录的 sequence。
 */
typedef struct {
    uint32_t inode;        /* Inode 编号 */
    uint32_t parent;       /* 父目录 Inode，根为 0 */
    uint32_t size;         /* 文件大小；目录为 0 */
    uint32_t version;      /* 版本号，越大越新 */
    uint32_t create_time;  /* 创建时间（秒） */
    uint32_t modify_time;  /* 修改时间（秒） */
    uint32_t epoch;        /* 数据 epoch，预留给整文件失效语义 */
    uint16_t name_len;     /* 名称长度 */
    uint8_t type;          /* MFS_TYPE_FILE / MFS_TYPE_DIR */
    uint8_t flags;         /* 保留 */
    char name[MFS_NAME_MAX + 1]; /* 名称，'\0' 结尾 */
    uint32_t rec_seq;      /* 该 Inode 最新记录的 sequence */
} mfs_ii_t;

/* ============================================================================
 * 小端序列化辅助
 *
 * 直接 memcpy C 结构体会受编译器 padding 和主机字节序影响，
 * 因此所有落盘字段都通过下面这些函数逐个字节读写。
 * ==========================================================================*/

static inline void mfs__put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static inline uint16_t mfs__get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline void mfs__put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static inline uint32_t mfs__get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* 向上对齐到 a 的整数倍；要求 a 是 2 的幂 */
static inline uint32_t mfs__align_up(uint32_t v, uint32_t a)
{
    return (v + a - 1u) & ~(a - 1u);
}

/* ============================================================================
 * CRC32（IEEE 802.3，多项式 0xEDB88320，无查表）
 * ==========================================================================*/

/* 以 crc 为运行值继续计算；首次调用传 0xFFFFFFFF，最后再异或 0xFFFFFFFF */
uint32_t mfs__crc32_update(uint32_t crc, const void *buf, uint32_t len);

/* 一次性计算一段数据的 CRC32 */
uint32_t mfs__crc32(const void *buf, uint32_t len);

/* ============================================================================
 * 块设备封装
 *
 * 统一做参数/回调检查，并把底层非 0 返回值映射为 MFS_ERR_IO。
 * 这些函数不负责加锁，锁由公开 API 层的 mfs__lock/mfs__unlock 负责。
 * ==========================================================================*/

int mfs__read(mfs_t *fs, uint32_t addr, void *buf, uint32_t len);
int mfs__prog(mfs_t *fs, uint32_t addr, const void *buf, uint32_t len);
int mfs__erase(mfs_t *fs, uint32_t addr, uint32_t len);
int mfs__sync(mfs_t *fs);

/* ============================================================================
 * 锁
 *
 * 若 config.lock/unlock 非空则调用；内部函数不再重复加锁。
 * ==========================================================================*/

void mfs__lock(mfs_t *fs);
void mfs__unlock(mfs_t *fs);

/* ============================================================================
 * Block 管理
 * ==========================================================================*/

/* 返回池内 block 的绝对起始地址：start + (block + 2) * block_size */
uint32_t mfs__blk_addr(mfs_t *fs, uint32_t block);

/* 读取并校验 Block Header；Header 无效（擦除/损坏）返回 NOENT/CRC */
int mfs__blk_read_hdr(mfs_t *fs, uint32_t block, mfs_blk_hdr_t *hdr);

/* 写入 Block Header 并同步；state 取 MFS_BLK_STATE_* */
int mfs__blk_write_hdr(mfs_t *fs, uint32_t block, uint32_t sequence,
                        uint32_t erase_count, uint32_t state);

/* 找到合适的空闲块并设为活动块（按擦除次数做磨损均衡） */
int mfs__blk_alloc(mfs_t *fs);

/* 将块标记为坏块（尽量持久化 BAD 头）；成功返回 OK */
int mfs__blk_mark_bad(mfs_t *fs, uint32_t block, uint32_t erase_count);

/* 块内是否还没有任何记录（empty=1 表示空闲） */
int mfs__blk_is_empty(mfs_t *fs, uint32_t block, uint8_t *empty);

/* ============================================================================
 * Record 读写与扫描
 * ==========================================================================*/

/*
 * 扫描回调。
 * hdr      : 已校验通过（Header CRC、Payload CRC、Commit Marker 都有效）的记录头；
 * rec_addr : 记录在介质中的绝对地址；
 * 返回非 0 会终止扫描并向上传递该错误。
 */
typedef int (*mfs_rec_cb_t)(mfs_t *fs, const mfs_rec_hdr_t *hdr,
                             uint32_t rec_addr, void *ctx);

/*
 * 追加写一条记录。
 * p1/l1 与 p2/l2 允许分两段提供 Payload（例如 DATA = 前缀 + 数据），
 * 避免为了拼接而复制大块数据。Payload CRC 会跨两段连续计算。
 * 写入顺序：Header -> Payload -> sync -> Commit Marker -> sync。
 */
int mfs__rec_write(mfs_t *fs, uint16_t type, uint32_t object_id,
                    const void *p1, uint32_t l1,
                    const void *p2, uint32_t l2);

/*
 * 读取并校验指定地址的记录。
 * payload 非空时读取完整 Payload 到该缓冲区（cap 必须足够）；
 * payload 为空时只做 CRC 校验，不搬运数据。
 * next_off 返回下一条记录的绝对地址（用于顺序扫描）。
 */
int mfs__rec_read(mfs_t *fs, uint32_t rec_addr, mfs_rec_hdr_t *hdr,
                   void *payload, uint32_t cap, uint32_t *next_off);

/* 读取 Payload 的指定片段（不重复校验 CRC，调用前应已确认记录有效） */
int mfs__rec_read_raw(mfs_t *fs, uint32_t rec_addr,
                       const mfs_rec_hdr_t *hdr, uint32_t payload_off,
                       void *dst, uint32_t len);

/*
 * 扫描单个块内的有效记录。
 * 遇到未提交/CRC 错误/0xFF 尾部即停止（认为块尾结束）。
 * last_off 返回最后一条有效记录之后的块内偏移。
 */
int mfs__scan_block(mfs_t *fs, uint32_t block, mfs_rec_cb_t cb,
                     void *ctx, uint32_t *last_off);

/* 扫描整个数据池；块头无效（空闲块）自动跳过 */
int mfs__scan_all(mfs_t *fs, mfs_rec_cb_t cb, void *ctx);

/* 计算一条记录占用的总字节数（Header + Payload + Padding + Marker） */
uint32_t mfs__rec_total_size(mfs_t *fs, uint32_t rec_off,
                              uint32_t payload_size);

/*
 * DATA 记录定位结果。
 * 文件读取与 GC 有效记录判定共用同一套“最新数据”语义：
 *   - 同一 (inode, chunk, epoch) 取 generation 最大的 DATA；
 *   - generation 相同时取 sequence 更大者；
 *   - 若存在 sequence 更大的 DATA_DELETE 覆盖该 Chunk，则旧数据失效。
 */
typedef struct {
    uint32_t inode;      /* 目标 Inode */
    uint32_t chunk;      /* 目标 Chunk 号 */
    uint32_t epoch;      /* 数据代次 */
    uint32_t gen;        /* 最新 DATA 的 generation */
    uint32_t seq;        /* 最新 DATA 的 sequence */
    uint32_t del_seq;    /* 覆盖该 Chunk 的最新 DATA_DELETE sequence */
    uint32_t addr;       /* 最新 DATA 记录地址 */
    uint16_t size;       /* 该 Chunk 有效字节数 */
    uint8_t found;       /* 是否找到 DATA 记录 */
} mfs_data_loc_t;

/*
 * 扫描全盘，定位指定 (inode, chunk, epoch) 的最新 DATA 记录
 * 以及覆盖它的最新 DATA_DELETE。
 */
int mfs__data_locate(mfs_t *fs, uint32_t inode, uint32_t chunk,
                      uint32_t epoch, mfs_data_loc_t *loc);

/* ============================================================================
 * Inode
 * ==========================================================================*/

/* 解析 INODE Payload；长度/name_len 异常返回 MFS_ERR_CORRUPT */
int mfs__inode_parse(const uint8_t *buf, uint32_t len, mfs_ii_t *info);

/* 查找 Inode 的最新有效版本；已删除或不存在返回 NOENT */
int mfs__inode_find(mfs_t *fs, mfs_inode_t inode, mfs_ii_t *info);

/*
 * 查找 Inode 的最新记录（包括删除记录）。
 * deleted 非空时输出 1 表示最新记录是 INODE_DELETE；
 * 删除时 info 仅 rec_seq 有效，其余字段为 0。
 */
int mfs__inode_find_ex(mfs_t *fs, mfs_inode_t inode, mfs_ii_t *info,
                        uint8_t *deleted);

/* 在指定父目录下按名称查找子项（大小写敏感） */
int mfs__inode_lookup(mfs_t *fs, mfs_inode_t parent, const char *name,
                       mfs_ii_t *info);

/* 创建 Inode 并写入 INODE_CREATE 记录；parent 为 0 时用于创建根 */
int mfs__inode_create(mfs_t *fs, mfs_inode_t parent, const char *name,
                       uint8_t type, mfs_ii_t *info);

/* 递增版本号、更新时间并写入 INODE_UPDATE 记录 */
int mfs__inode_update(mfs_t *fs, mfs_ii_t *info);

/* 写入 INODE_DELETE 记录（版本号自动 +1） */
int mfs__inode_delete(mfs_t *fs, const mfs_ii_t *info);

/* 按记录类型序列化并写入 Inode（内部复用） */
int mfs__inode_write(mfs_t *fs, uint16_t rec_type, const mfs_ii_t *info);

/* ============================================================================
 * 路径
 * ==========================================================================*/

/* 解析完整路径；"/" 返回根目录信息 */
int mfs__path_lookup(mfs_t *fs, const char *path, mfs_ii_t *info);

/* 解析父目录和最后一段名称；路径为 "/" 时返回 INVAL */
int mfs__path_parent(mfs_t *fs, const char *path, mfs_ii_t *parent,
                      char *name);

/* ============================================================================
 * 时间
 * ==========================================================================*/

/* 读取 config.get_time，未配置时返回 0 */
uint32_t mfs__now(mfs_t *fs);

/* ============================================================================
 * GC
 * ==========================================================================*/

/*
 * 执行一次 GC。
 * force = 0：只回收有效数据比例低于阈值的块；
 * force = 1：空间不足时允许回收任意可回收块。
 * 返回 OK / NOSPC / BUSY（有目录句柄打开）等。
 */
int mfs__gc_run(mfs_t *fs, uint8_t force);

#endif /* MFS_INTERNAL_H */
