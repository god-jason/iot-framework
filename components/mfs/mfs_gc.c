/*
 * mfs_gc.c - 垃圾回收、有效记录判定与磨损均衡选块
 * ============================================================================
 *
 * GC 目标：
 *   - 回收被更新/删除/截断淘汰的旧记录；
 *   - 把牺牲块中的有效记录复制到新位置；
 *   - 擦除牺牲块，使其重新进入空闲池。
 *
 * 掉电安全（先复制、后擦除）：
 *   1. 复制有效记录到当前日志，每条记录独立提交；
 *   2. 同步；
 *   3. 擦除牺牲块并写 FREE 头。
 *   若在 1/2 之后、3 之前掉电：新旧记录同时存在，读取按
 *   version/generation/sequence 选择新副本，数据仍一致；下次 GC
 *   会发现旧块已无有效记录并直接回收。
 *
 * 选块策略：
 *   - 跳过活动块、坏块、正在 GC 的块；
 *   - 计算块内有效字节比例；
 *   - 默认只回收有效比例 < 75% 的块；
 *   - 比例相同时优先选择擦除次数高的块（磨损均衡）。
 *
 * 约束：
 *   - 有目录句柄打开时禁止 GC，避免 readdir 看到复制出的新记录而重复；
 *   - GC 只在已加锁的上下文执行（公开 API 或内部写路径）。
 */

#include "mfs_internal.h"

/* 有效数据比例上限（百分比），超过则不选为牺牲块 */
#define MFS_GC_LIVE_MAX      75u

/* ---------------- 有效记录判定 ---------------- */

/*
 * 判断一条记录是否仍然有效（GC 需要保留）。
 * 返回 1=有效，0=可回收，负值=读取错误。
 *
 * INODE：最新一条记录（version/sequence 最大）有效；删除记录本身
 *        在 Inode 已删除时保留，避免计数器/审计信息丢失。
 * DATA ：仅当它是该 (inode, chunk, epoch) 最新的 DATA，且未被
 *        更晚的 DATA_DELETE 覆盖时有效。
 * DATA_DELETE：Inode 仍存在且 epoch 一致时保留。
 */
static int record_is_live(mfs_t *fs, const mfs_rec_hdr_t *hdr,
                          uint32_t rec_addr)
{
    mfs_ii_t info;
    uint8_t deleted = 0u;
    uint8_t buf[MFS_DATA_PREFIX];
    int err;

    switch (hdr->type) {
    case MFS_REC_INODE_CREATE:
    case MFS_REC_INODE_UPDATE:
    case MFS_REC_INODE_DELETE:
        err = mfs__inode_find_ex(fs, hdr->object_id, &info, &deleted);
        if (err == MFS_ERR_NOENT) {
            return 0;
        }
        if (err != MFS_OK) {
            return err;
        }
        if (info.rec_seq != hdr->sequence) {
            return 0;                    /* 已有更新版本 */
        }
        if (hdr->type == MFS_REC_INODE_DELETE) {
            return (deleted != 0u) ? 1 : 0;
        }
        return (deleted == 0u) ? 1 : 0;

    case MFS_REC_DATA: {
        uint32_t chunk;
        uint32_t epoch;
        mfs_data_loc_t loc;

        if (hdr->payload_size < MFS_DATA_PREFIX) {
            return 0;
        }
        err = mfs__rec_read_raw(fs, rec_addr, hdr, 0u, buf, sizeof(buf));
        if (err != MFS_OK) {
            return err;
        }
        chunk = mfs__get_u32(&buf[4]);
        epoch = mfs__get_u32(&buf[12]);

        err = mfs__inode_find(fs, hdr->object_id, &info);
        if (err == MFS_ERR_NOENT) {
            return 0;                    /* 文件已删除 */
        }
        if (err != MFS_OK) {
            return err;
        }
        if (epoch != info.epoch) {
            return 0;
        }
        /* Chunk 已超出文件大小：截断残留 */
        if (info.size == 0u ||
            chunk > (info.size - 1u) / MFS_CHUNK_SIZE) {
            return 0;
        }

        /* 复用与文件读取相同的最新数据判定 */
        err = mfs__data_locate(fs, hdr->object_id, chunk, epoch, &loc);
        if (err != MFS_OK) {
            return err;
        }
        return (loc.found && loc.seq == hdr->sequence &&
                loc.seq > loc.del_seq) ? 1 : 0;
    }

    case MFS_REC_DATA_DELETE:
        if (hdr->payload_size < 16u) {
            return 0;
        }
        err = mfs__rec_read_raw(fs, rec_addr, hdr, 0u, buf, 16u);
        if (err != MFS_OK) {
            return err;
        }
        err = mfs__inode_find(fs, hdr->object_id, &info);
        if (err != MFS_OK) {
            return 0;                    /* 文件已删除，删除记录可回收 */
        }
        return (mfs__get_u32(&buf[12]) == info.epoch) ? 1 : 0;

    default:
        /* 未知/预留记录：保守保留 */
        return 1;
    }
}

/* ---------------- 块内有效数据统计 ---------------- */

typedef struct {
    uint32_t base;      /* 块起始绝对地址 */
    uint32_t live;      /* 有效记录字节数 */
} gc_stat_ctx_t;

static int gc_stat_cb(mfs_t *fs, const mfs_rec_hdr_t *hdr,
                      uint32_t rec_addr, void *ctx)
{
    gc_stat_ctx_t *s = (gc_stat_ctx_t *)ctx;
    int live = record_is_live(fs, hdr, rec_addr);

    if (live < 0) {
        return live;
    }
    if (live != 0) {
        s->live += mfs__rec_total_size(fs, rec_addr - s->base,
                                        hdr->payload_size);
    }
    return MFS_OK;
}

/* ---------------- 有效记录复制 ---------------- */

/*
 * 复制一条记录到当前日志。
 * 复制后 sequence 变化，version/generation 保持不变：
 *   - INODE：版本号相同，sequence 更大 -> 副本成为最新；
 *   - DATA ：generation 相同，sequence 更大 -> 副本成为最新。
 */
static int gc_copy_record(mfs_t *fs, const mfs_rec_hdr_t *hdr,
                          uint32_t rec_addr)
{
    uint32_t size = hdr->payload_size;
    int err;

    if (hdr->type == MFS_REC_DATA) {
        uint8_t prefix[MFS_DATA_PREFIX];
        uint32_t data_size;

        if (size < MFS_DATA_PREFIX) {
            return MFS_ERR_CORRUPT;
        }
        data_size = size - MFS_DATA_PREFIX;
        if (data_size > MFS_CHUNK_SIZE || data_size > fs->workbuf_size) {
            return MFS_ERR_CORRUPT;
        }
        err = mfs__rec_read_raw(fs, rec_addr, hdr, 0u, prefix,
                                 sizeof(prefix));
        if (err != MFS_OK) {
            return err;
        }
        err = mfs__rec_read_raw(fs, rec_addr, hdr, MFS_DATA_PREFIX,
                                 fs->workbuf, data_size);
        if (err != MFS_OK) {
            return err;
        }
        return mfs__rec_write(fs, hdr->type, hdr->object_id,
                               prefix, sizeof(prefix),
                               fs->workbuf, data_size);
    }

    if (size > fs->workbuf_size) {
        return MFS_ERR_CORRUPT;
    }
    err = mfs__rec_read_raw(fs, rec_addr, hdr, 0u, fs->workbuf, size);
    if (err != MFS_OK) {
        return err;
    }
    return mfs__rec_write(fs, hdr->type, hdr->object_id,
                           fs->workbuf, size, NULL, 0u);
}

/* 复制回调：只搬移仍然有效的记录 */
static int gc_copy_cb(mfs_t *fs, const mfs_rec_hdr_t *hdr,
                      uint32_t rec_addr, void *ctx)
{
    int live = record_is_live(fs, hdr, rec_addr);

    (void)ctx;
    if (live < 0) {
        return live;
    }
    if (live == 0) {
        return MFS_OK;
    }
    return gc_copy_record(fs, hdr, rec_addr);
}

/* ---------------- GC 主流程 ---------------- */

static int gc_has_open_dir(mfs_t *fs)
{
    uint32_t i;

    for (i = 0u; i < fs->cfg.max_open_dirs; i++) {
        if (fs->dirs[i].used != 0u) {
            return 1;
        }
    }
    return 0;
}

int mfs__gc_run(mfs_t *fs, uint8_t force)
{
    uint32_t block;
    uint32_t victim = MFS_BLOCK_NONE;
    uint32_t best_ratio = 0xFFFFFFFFu;
    uint32_t best_wear = 0u;
    mfs_blk_hdr_t vh;
    uint32_t last = 0u;
    uint32_t usable;
    int err;

    if (fs == NULL || !fs->mounted) {
        return MFS_ERR_STATE;
    }
    if (fs->readonly) {
        return MFS_ERR_READONLY;
    }
    if (fs->gc_running) {
        return MFS_ERR_BUSY;
    }
    if (gc_has_open_dir(fs)) {
        return MFS_ERR_BUSY;            /* readdir 迭代期间不搬移记录 */
    }

    usable = fs->block_size - MFS_BLK_HDR_SIZE;

    /* 1. 选择牺牲块：有效数据比例最低，比例相同优先高磨损块 */
    for (block = 0u; block < fs->block_count; block++) {
        mfs_blk_hdr_t bh;
        gc_stat_ctx_t st;
        uint32_t ratio;

        if (block == fs->active_block || block == fs->gc_block) {
            continue;
        }
        err = mfs__blk_read_hdr(fs, block, &bh);
        if (err != MFS_OK) {
            continue;                    /* 空闲/坏块/无效块 */
        }
        if (bh.state == MFS_BLK_STATE_BAD) {
            continue;
        }

        memset(&st, 0, sizeof(st));
        st.base = mfs__blk_addr(fs, block);
        err = mfs__scan_block(fs, block, gc_stat_cb, &st, &last);
        if (err != MFS_OK) {
            return err;
        }
        if (last == MFS_BLK_HDR_SIZE) {
            uint8_t empty = 0u;

            err = mfs__blk_is_empty(fs, block, &empty);
            if (err != MFS_OK) {
                return err;
            }
            if (empty != 0u) {
                continue;                /* 真正空块 */
            }
            /* 只有写坏的记录头：按 live=0 的垃圾块回收 */
            st.live = 0u;
        }

        ratio = (usable != 0u)
                    ? (uint32_t)(((uint64_t)st.live * 100u) / usable)
                    : 100u;
        if (st.live != 0u && !force && ratio >= MFS_GC_LIVE_MAX) {
            continue;                    /* 有效数据太多，不值得搬 */
        }

        if (victim == MFS_BLOCK_NONE || ratio < best_ratio ||
            (ratio == best_ratio && bh.erase_count > best_wear)) {
            victim = block;
            best_ratio = ratio;
            best_wear = bh.erase_count;
        }
    }

    if (victim == MFS_BLOCK_NONE) {
        return force ? MFS_ERR_NOSPC : MFS_OK;
    }

    /* 记录牺牲块头信息（擦除后需要沿用擦除次数） */
    err = mfs__blk_read_hdr(fs, victim, &vh);
    if (err != MFS_OK) {
        return err;
    }

    fs->gc_running = 1u;
    fs->gc_block = victim;

    /* 2. 复制有效记录；任何失败都保留原块 */
    err = mfs__scan_block(fs, victim, gc_copy_cb, NULL, &last);
    if (err == MFS_OK) {
        err = mfs__sync(fs);
    }
    if (err != MFS_OK) {
        fs->gc_block = MFS_BLOCK_NONE;
        fs->gc_running = 0u;
        return err;
    }

    /* 3. 擦除牺牲块并写 FREE 头 */
    err = mfs__erase(fs, mfs__blk_addr(fs, victim), fs->block_size);
    if (err != MFS_OK) {
        /* 尽量持久化坏块标记；原数据已被复制，文件系统仍可用 */
        (void)mfs__blk_mark_bad(fs, victim, vh.erase_count + 1u);
        fs->bad_blocks++;
        fs->gc_block = MFS_BLOCK_NONE;
        fs->gc_running = 0u;
        return MFS_ERR_IO;
    }

    if (fs->free_blocks < fs->block_count) {
        fs->free_blocks++;
    }
    if (fs->used_bytes >= fs->block_size) {
        fs->used_bytes -= fs->block_size;
    } else {
        fs->used_bytes = 0u;
    }

    err = mfs__blk_write_hdr(fs, victim, fs->seq++, vh.erase_count + 1u,
                              MFS_BLK_STATE_FREE);
    fs->gc_block = MFS_BLOCK_NONE;
    fs->gc_running = 0u;
    return err;
}
