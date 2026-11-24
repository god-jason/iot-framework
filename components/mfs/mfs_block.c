/*
 * mfs_block.c - Block Header、块分配、空闲判定与坏块标记
 * ============================================================================
 *
 * Block Header（24B，小端）：
 *
 *   +0  magic       4B   "!BLK"
 *   +4  sequence    4B   擦除顺序，越大越新
 *   +8  erase_count 4B   累计擦除次数（磨损均衡）
 *   +12 state       4B   FREE / BAD
 *   +16 crc         4B   前 16B 的 CRC32
 *   +20 reserved    4B   保留，写 0
 *
 * 块的生命周期：
 *   - 擦除后写入 FREE 头；
 *   - 分配时只把块设为“当前活动块”（RAM），不改头；
 *   - 是否已使用由块内是否存在有效 Record 判断；
 *   - GC 搬空后重新擦除并写 FREE 头，擦除次数 +1；
 *   - 擦除失败的块写 BAD 头，之后分配/GC 永久跳过。
 *
 * 磨损均衡：
 *   分配时从 alloc_hint 开始轮转扫描，选择 erase_count 最小的 FREE 块；
 *   擦除次数相同时按轮转顺序取第一个，避免总是使用低编号块。
 *
 * 块设备回调封装见 mfs_blockdev.c。
 */

#include "mfs_internal.h"

/* 数据池块的绝对地址：前两个块留给 Superblock A/B */
uint32_t mfs__blk_addr(mfs_t *fs, uint32_t block)
{
    return fs->dev.start_addr + (block + 2u) * fs->block_size;
}

/*
 * 读取并校验 Block Header。
 * 返回：
 *   MFS_OK         头有效；
 *   MFS_ERR_NOENT  魔数/状态无效（未初始化或已擦除）；
 *   MFS_ERR_CRC    CRC 错误；
 *   MFS_ERR_IO     读取失败。
 */
int mfs__blk_read_hdr(mfs_t *fs, uint32_t block, mfs_blk_hdr_t *hdr)
{
    uint8_t buf[MFS_BLK_HDR_SIZE];
    uint32_t crc;
    int err;

    if (fs == NULL || hdr == NULL || block >= fs->block_count) {
        return MFS_ERR_PARAM;
    }

    err = mfs__read(fs, mfs__blk_addr(fs, block), buf, sizeof(buf));
    if (err != MFS_OK) {
        return err;
    }

    hdr->magic = mfs__get_u32(&buf[0]);
    hdr->sequence = mfs__get_u32(&buf[4]);
    hdr->erase_count = mfs__get_u32(&buf[8]);
    hdr->state = mfs__get_u32(&buf[12]);
    hdr->crc = mfs__get_u32(&buf[16]);
    hdr->reserved = mfs__get_u32(&buf[20]);

    if (hdr->magic != MFS_BLK_MAGIC) {
        return MFS_ERR_NOENT;
    }
    crc = mfs__crc32(buf, 16u);
    if (crc != hdr->crc) {
        return MFS_ERR_CRC;
    }
    if (hdr->state != MFS_BLK_STATE_FREE &&
        hdr->state != MFS_BLK_STATE_BAD) {
        return MFS_ERR_NOENT;
    }
    return MFS_OK;
}

/*
 * 写入 Block Header 并同步。
 * 只能在擦除后的块上调用（或用于覆盖一个已知无效的头部）。
 */
int mfs__blk_write_hdr(mfs_t *fs, uint32_t block, uint32_t sequence,
                        uint32_t erase_count, uint32_t state)
{
    uint8_t buf[MFS_BLK_HDR_SIZE];
    int err;

    if (fs == NULL || block >= fs->block_count) {
        return MFS_ERR_PARAM;
    }

    mfs__put_u32(&buf[0], MFS_BLK_MAGIC);
    mfs__put_u32(&buf[4], sequence);
    mfs__put_u32(&buf[8], erase_count);
    mfs__put_u32(&buf[12], state);
    /* CRC 只覆盖 magic/sequence/erase_count/state，不包含自身与保留字段 */
    mfs__put_u32(&buf[16], mfs__crc32(buf, 16u));
    mfs__put_u32(&buf[20], 0u);

    err = mfs__prog(fs, mfs__blk_addr(fs, block), buf, sizeof(buf));
    if (err != MFS_OK) {
        return err;
    }
    return mfs__sync(fs);
}

/* 判断块内是否还没有任何记录（首个 4 字节仍为 0xFF） */
int mfs__blk_is_empty(mfs_t *fs, uint32_t block, uint8_t *empty)
{
    uint32_t magic;
    int err;

    err = mfs__read(fs, mfs__blk_addr(fs, block) + MFS_BLK_HDR_SIZE,
                     &magic, 4u);
    if (err != MFS_OK) {
        return err;
    }
    *empty = (magic == 0xFFFFFFFFu) ? 1u : 0u;
    return MFS_OK;
}

/*
 * 把块标记为坏块。
 * 调用者应保证块已被擦除；本函数只写 BAD 头。
 * 返回头写入结果。
 */
int mfs__blk_mark_bad(mfs_t *fs, uint32_t block, uint32_t erase_count)
{
    return mfs__blk_write_hdr(fs, block, fs->seq++, erase_count,
                               MFS_BLK_STATE_BAD);
}

/*
 * 分配一个空闲块作为活动块。
 *
 * 候选：
 *   - 状态 FREE 且块内无记录；
 *   - 未初始化（头无效/已擦除），需要先擦除并写 FREE 头。
 *
 * 选择 erase_count 最小的候选，从 alloc_hint 开始轮转。
 * 擦除失败时尝试标记 BAD，并继续寻找下一个候选。
 */
static int blk_alloc_impl(mfs_t *fs, bool allow_reserve)
{
    uint32_t tries;

    if (fs == NULL || fs->readonly) {
        return MFS_ERR_READONLY;
    }

    /*
     * 预留一个空闲块给 GC：普通写入不能在池子耗尽时才触发回收，
     * 否则 GC 复制有效记录时没有目的块可用。
     * Inode/删除类元数据记录（allow_reserve）必须能写入，否则文件系统
     * 写满后连 rm 都无法执行，永远无法释放空间。
     */
    if (fs->block_count > MFS_GC_RESERVE && fs->gc_running == 0u &&
        !allow_reserve && fs->free_blocks <= MFS_GC_RESERVE) {
        return MFS_ERR_NOSPC;
    }

    /*
     * 正常情况下最多一轮就能找到候选；
     * 只有“未初始化块擦除失败”会消耗一个候选并重试。
     */
    for (tries = 0u; tries <= fs->block_count; tries++) {
        uint32_t start;
        uint32_t best = MFS_BLOCK_NONE;
        uint32_t best_count = 0xFFFFFFFFu;
        uint8_t best_uninit = 0u;
        uint32_t i;
        int err;

        start = (fs->block_count != 0u)
                    ? (fs->alloc_hint % fs->block_count) : 0u;

        /* 从 alloc_hint 轮转扫描，选擦除次数最低的空闲块 */
        for (i = 0u; i < fs->block_count; i++) {
            uint32_t block = (start + i) % fs->block_count;
            mfs_blk_hdr_t bh;
            uint8_t empty = 0u;

            if (block == fs->active_block || block == fs->gc_block) {
                continue;
            }

            err = mfs__blk_read_hdr(fs, block, &bh);
            if (err == MFS_ERR_NOENT || err == MFS_ERR_CRC) {
                /* 未初始化：按擦除次数 0 处理，优先使用 */
                if (best == MFS_BLOCK_NONE || best_count > 0u) {
                    best = block;
                    best_count = 0u;
                    best_uninit = 1u;
                }
                continue;
            }
            if (err != MFS_OK) {
                return err;
            }
            if (bh.state == MFS_BLK_STATE_BAD) {
                continue;
            }

            err = mfs__blk_is_empty(fs, block, &empty);
            if (err != MFS_OK) {
                return err;
            }
            if (empty == 0u) {
                continue;                /* FREE 头但已有记录：视为已用 */
            }
            if (bh.erase_count < best_count) {
                best = block;
                best_count = bh.erase_count;
                best_uninit = 0u;
            }
        }

        if (best == MFS_BLOCK_NONE) {
            return MFS_ERR_NOSPC;
        }

        /* 未初始化块：擦除并写 FREE 头，擦除次数从 1 开始 */
        if (best_uninit != 0u) {
            err = mfs__erase(fs, mfs__blk_addr(fs, best), fs->block_size);
            if (err != MFS_OK) {
                (void)mfs__blk_mark_bad(fs, best, 1u);
                fs->bad_blocks++;
                continue;                /* 换下一个候选 */
            }
            err = mfs__blk_write_hdr(fs, best, fs->seq++, 1u,
                                      MFS_BLK_STATE_FREE);
            if (err != MFS_OK) {
                return err;
            }
            best_count = 1u;
        }

        fs->active_block = best;
        fs->active_off = MFS_BLK_HDR_SIZE;
        fs->erase_count = best_count;
        if (fs->free_blocks > 0u) {
            fs->free_blocks--;
        }
        fs->used_bytes += fs->block_size;
        fs->alloc_hint = (best + 1u) % fs->block_count;
        return MFS_OK;
    }

    return MFS_ERR_NOSPC;
}

/* 普通数据分配：保留 GC 预留块 */
int mfs__blk_alloc(mfs_t *fs)
{
    return blk_alloc_impl(fs, false);
}

/* 元数据/删除记录分配：空间不足时允许使用 GC 预留块 */
int mfs__blk_alloc_meta(mfs_t *fs)
{
    return blk_alloc_impl(fs, true);
}
