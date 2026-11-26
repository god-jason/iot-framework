/*
 * mfs_maint.c - 维护接口：GC、磨损统计、坏块标记、预留事务与错误字符串
 * ============================================================================
 *
 * 本文件只包含“运行期维护”相关逻辑，生命周期（init/format/mount）
 * 与空间统计仍在 MFS.c。GC 具体算法在 mfs_gc.c。
 *
 * 线程模型：
 *   - 每个公开接口进入时加锁；
 *   - mfs_gc / mfs_mark_bad 允许在已有文件句柄打开时调用，
 *     但存在打开目录句柄时 GC 返回 BUSY（避免 readdir 重复枚举）。
 */

#include "mfs_internal.h"

/*
 * 统计有效块头的擦除次数。
 * 空闲块（Header 无效）没有计数，不参与统计。
 */
int mfs_erase_stat(mfs_t *fs, uint32_t *min_count,
                    uint32_t *max_count, uint32_t *avg_count)
{
    mfs_blk_hdr_t bh;
    uint32_t block;
    uint32_t count = 0u;
    uint32_t sum = 0u;
    uint32_t min_v = 0xFFFFFFFFu;
    uint32_t max_v = 0u;

    if (fs == NULL || min_count == NULL || max_count == NULL ||
        avg_count == NULL) {
        return MFS_ERR_PARAM;
    }

    mfs_lock(fs);
    for (block = 0; block < fs->block_count; block++) {
        if (mfs_blk_read_hdr(fs, block, &bh) != MFS_OK) {
            continue;
        }
        if (bh.erase_count < min_v) {
            min_v = bh.erase_count;
        }
        if (bh.erase_count > max_v) {
            max_v = bh.erase_count;
        }
        sum += bh.erase_count;
        count++;
    }
    mfs_unlock(fs);

    if (count == 0u) {
        return MFS_ERR_NOENT;
    }
    *min_count = min_v;
    *max_count = max_v;
    *avg_count = sum / count;
    return MFS_OK;
}

/* ============================================================================
 * 事务 / Checkpoint（V1 占位）
 * ==========================================================================*/

/* 多记录事务尚未实现 */
int mfs_tx_begin(mfs_t *fs)
{
    (void)fs;
    return MFS_ERR_NOTSUP;
}

int mfs_tx_commit(mfs_t *fs)
{
    (void)fs;
    return MFS_ERR_NOTSUP;
}

int mfs_tx_abort(mfs_t *fs)
{
    (void)fs;
    return MFS_ERR_NOTSUP;
}

/* Checkpoint 尚未实现 */
int mfs_checkpoint(mfs_t *fs)
{
    (void)fs;
    return MFS_ERR_NOTSUP;
}

/* ============================================================================
 * GC 与坏块
 * ==========================================================================*/

/* 手动 GC：只回收有效数据比例低于阈值的块 */
int mfs_gc(mfs_t *fs)
{
    int err;

    if (fs == NULL || !fs->mounted) {
        return MFS_ERR_STATE;
    }
    if (fs->readonly) {
        return MFS_ERR_READONLY;
    }

    mfs_lock(fs);
    err = mfs_gc_run(fs, 0u);
    mfs_unlock(fs);
    return err;
}

/* 空闲空间低于阈值或空闲块不足 2 个时建议执行 GC */
int mfs_gc_needed(mfs_t *fs)
{
    mfs_statfs_t stat;
    int err;

    if (fs == NULL) {
        return MFS_ERR_PARAM;
    }
    err = mfs_statfs(fs, &stat);
    if (err != MFS_OK) {
        return err;
    }
    if (stat.free_blocks <= 1u) {
        return 1;
    }
    if (fs->cfg.gc_threshold == 0u) {
        return 0;
    }
    return (stat.free_bytes < fs->cfg.gc_threshold) ? 1 : 0;
}

/*
 * 手动把某个块标记为坏块。
 * 会擦除该块，原有数据丢失；活动块/正在 GC 的块不允许标记。
 * 已经是坏块时返回 MFS_ERR_INVAL，避免重复计数。
 */
int mfs_mark_bad(mfs_t *fs, uint32_t block)
{
    mfs_blk_hdr_t bh;
    uint8_t empty = 1u;
    uint32_t erase_count = 1u;
    int err;

    if (fs == NULL || !fs->mounted) {
        return MFS_ERR_STATE;
    }
    if (fs->readonly) {
        return MFS_ERR_READONLY;
    }
    if (block >= fs->block_count ||
        block == fs->active_block || block == fs->gc_block) {
        return MFS_ERR_INVAL;
    }

    mfs_lock(fs);

    if (mfs_blk_read_hdr(fs, block, &bh) == MFS_OK) {
        if (bh.state == MFS_BLK_STATE_BAD) {
            mfs_unlock(fs);
            return MFS_ERR_INVAL;       /* 已经是坏块，避免重复计数 */
        }
        erase_count = bh.erase_count + 1u;
        err = mfs_blk_is_empty(fs, block, &empty);
        if (err != MFS_OK) {
            mfs_unlock(fs);
            return err;
        }
    } else {
        empty = 1u;                      /* 头无效：挂载时已按空闲块统计 */
    }

    err = mfs_erase(fs, mfs_blk_addr(fs, block), fs->block_size);
    if (err != MFS_OK) {
        mfs_unlock(fs);
        return err;
    }
    err = mfs_blk_mark_bad(fs, block, erase_count);
    if (err != MFS_OK) {
        mfs_unlock(fs);
        return err;
    }

    if (empty != 0u && fs->free_blocks > 0u) {
        fs->free_blocks--;
    }
    fs->bad_blocks++;
    mfs_unlock(fs);
    return MFS_OK;
}

/* ============================================================================
 * 错误码
 * ==========================================================================*/

/* 错误码到可读字符串 */
const char *mfs_strerror(int error)
{
    switch (error) {
    case MFS_OK:               return "ok";
    case MFS_ERR_PARAM:        return "invalid parameter";
    case MFS_ERR_IO:           return "io error";
    case MFS_ERR_CRC:          return "crc error";
    case MFS_ERR_FORMAT:       return "not formatted";
    case MFS_ERR_VERSION:      return "version mismatch";
    case MFS_ERR_NOENT:        return "no such entry";
    case MFS_ERR_EXIST:        return "already exists";
    case MFS_ERR_NOTDIR:       return "not a directory";
    case MFS_ERR_ISDIR:        return "is a directory";
    case MFS_ERR_NOSPC:        return "no space left";
    case MFS_ERR_NOMEM:        return "no memory";
    case MFS_ERR_BUSY:         return "busy";
    case MFS_ERR_INVAL:        return "invalid argument";
    case MFS_ERR_READONLY:     return "read only";
    case MFS_ERR_CORRUPT:      return "corrupted";
    case MFS_ERR_NOTEMPTY:     return "not empty";
    case MFS_ERR_BADF:         return "bad file handle";
    case MFS_ERR_NOTSUP:       return "not supported";
    case MFS_ERR_TIMEOUT:      return "timeout";
    case MFS_ERR_STATE:        return "invalid state";
    case MFS_ERR_POWERLOSS:    return "power loss";
    case MFS_ERR_BADBLOCK:     return "bad block";
    default:                    return "unknown error";
    }
}
