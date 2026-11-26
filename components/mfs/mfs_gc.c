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
 * 有效数据判定（1.2 起）：
 *   旧实现逐条记录调用 mfs_inode_find()/mfs_data_locate()，每条记录
 *   都要全盘扫描，整体退化为 O(记录数²)，2MB 写满时几乎无法完成。
 *   现在先收集候选块的全部记录，再对整个日志做一次扫描：
 *     - 同 (inode, chunk, epoch) 的更新记录使旧记录失效；
 *     - DATA_DELETE 按范围使旧记录失效；
 *     - 同时记录每个 Inode 的最新记录，用于判断 Inode/文件是否仍存在。
 *   整体复杂度 O(日志大小 × 块内记录数)，块内记录数上限 64。
 *
 * 选块策略：
 *   - 跳过活动块、坏块、正在 GC 的块；
 *   - 按块头 sequence 从旧到新尝试，最多 MFS_GC_TRY_BLOCKS 个块；
 *   - 默认只回收有效比例 < 75% 的块；
 *   - 有效数据为 0（纯垃圾/坏记录头）的块立即选中。
 *
 * 空间预留：
 *   mfs_blk_alloc() 在 free_blocks <= MFS_GC_RESERVE 时拒绝普通分配
 *   （GC 自身除外），保证 GC 总有一个块可以承载复制出的有效记录。
 *
 * 约束：
 *   - 有目录句柄打开时禁止 GC，避免 readdir 看到复制出的新记录而重复；
 *   - GC 只在已加锁的上下文执行（公开 API 或内部写路径）。
 */

#include "mfs_internal.h"

/* 有效数据比例上限（百分比），超过则不选为牺牲块 */
#define MFS_GC_LIVE_MAX      75u

/*
 * 强制回收（空间不足时）也要求块内至少有 10% 可回收空间。
 * 否则像顺序追加写满这种场景，GC 会为回收几十字节垃圾而搬移整块有效数据，
 * 既没有收益又非常耗时（每次评估都要全盘扫描一遍）。
 */
#define MFS_GC_FORCE_LIVE_MAX 90u

/* 单个块最多参与评估的记录数；超过则该块本轮不回收（保守） */
#define MFS_GC_MAX_RECS      64u

/* 单次 GC 最多评估的块数（按 sequence 从旧到新） */
#define MFS_GC_TRY_BLOCKS    2u

/* Inode Payload 读取缓冲（前缀 + 最长名称 + 余量） */
#define MFS_GC_INODE_BUF     96u

/* 一条候选记录 */
typedef struct {
    uint32_t addr;          /* 记录绝对地址 */
    uint32_t seq;           /* 记录 sequence */
    uint32_t object_id;     /* Inode 编号 */
    uint32_t chunk;         /* DATA: chunk；DATA_DELETE: start */
    uint32_t count;         /* DATA_DELETE: 覆盖数量 */
    uint32_t epoch;
    uint32_t payload_size;
    uint16_t type;
    uint8_t  live;          /* 评估结果：1 有效 */
} gc_rec_t;

typedef struct {
    uint32_t base;                      /* 块起始绝对地址 */
    uint32_t count;
    uint8_t  overflow;                  /* 记录数超出上限 */
    gc_rec_t recs[MFS_GC_MAX_RECS];
} gc_block_t;

typedef struct {
    gc_rec_t *recs;
    uint32_t  count;
    uint32_t  inode_seq[MFS_GC_MAX_RECS];   /* 各候选对应的最新 Inode 记录 */
    uint32_t  inode_addr[MFS_GC_MAX_RECS];
    uint16_t  inode_type[MFS_GC_MAX_RECS];
} gc_eval_t;

/* ---------------- 收集块内记录 ---------------- */

static int gc_collect_cb(mfs_t *fs, const mfs_rec_hdr_t *hdr,
                         uint32_t rec_addr, void *ctx)
{
    gc_block_t *b = (gc_block_t *)ctx;
    uint8_t buf[MFS_DATA_PREFIX];
    uint32_t need;
    gc_rec_t *r;
    int err;

    if (b->count >= MFS_GC_MAX_RECS) {
        b->overflow = 1u;
        return MFS_ERR_NOSPC;           /* 中止扫描 */
    }
    r = &b->recs[b->count++];
    memset(r, 0, sizeof(*r));
    r->addr = rec_addr;
    r->seq = hdr->sequence;
    r->object_id = hdr->object_id;
    r->payload_size = hdr->payload_size;
    r->type = hdr->type;
    r->live = 1u;

    need = 0u;
    if (hdr->type == MFS_REC_DATA) {
        need = MFS_DATA_PREFIX;
    } else if (hdr->type == MFS_REC_DATA_DELETE) {
        need = 16u;
    }
    if (need != 0u) {
        if (hdr->payload_size < need) {
            r->live = 0u;               /* 坏记录，直接可回收 */
            return MFS_OK;
        }
        err = mfs_rec_read_raw(fs, rec_addr, hdr, 0u, buf, need);
        if (err != MFS_OK) {
            return err;
        }
        r->epoch = mfs_get_u32(&buf[12]);
        r->chunk = mfs_get_u32(&buf[4]);
        if (hdr->type == MFS_REC_DATA_DELETE) {
            r->count = mfs_get_u32(&buf[8]);
        }
    }
    return MFS_OK;
}

/* ---------------- 单遍评估 ---------------- */

static bool gc_is_inode_rec(uint16_t type)
{
    return type == MFS_REC_INODE_CREATE || type == MFS_REC_INODE_UPDATE ||
           type == MFS_REC_INODE_DELETE;
}

/*
 * 扫描整个日志一次，把所有候选记录与日志中的其他记录比较：
 *   - 更新的同 (inode, chunk, epoch) DATA 使旧 DATA 失效；
 *   - 覆盖该 Chunk 的 DATA_DELETE 使旧 DATA 失效；
 *   - 更新的 INODE 记录使旧 INODE 记录失效，并记录每个 Inode 的最新记录。
 */
static int gc_eval_cb(mfs_t *fs, const mfs_rec_hdr_t *hdr,
                      uint32_t rec_addr, void *ctx)
{
    gc_eval_t *e = (gc_eval_t *)ctx;
    uint8_t buf[MFS_DATA_PREFIX];
    uint32_t r_chunk = 0u;
    uint32_t r_count = 0u;
    uint32_t r_epoch = 0u;
    bool prefix_ok = false;
    uint32_t i;
    int err;

    for (i = 0u; i < e->count; i++) {
        gc_rec_t *c = &e->recs[i];

        if (c->addr == rec_addr || c->object_id != hdr->object_id) {
            continue;
        }

        if (gc_is_inode_rec(hdr->type)) {
            if (hdr->sequence > e->inode_seq[i]) {
                e->inode_seq[i] = hdr->sequence;
                e->inode_addr[i] = rec_addr;
                e->inode_type[i] = hdr->type;
            }
            if (gc_is_inode_rec(c->type) && hdr->sequence > c->seq) {
                c->live = 0u;
            }
            continue;
        }

        if (c->type != MFS_REC_DATA ||
            (hdr->type != MFS_REC_DATA && hdr->type != MFS_REC_DATA_DELETE)) {
            continue;
        }
        if (!prefix_ok) {
            uint32_t need = (hdr->type == MFS_REC_DATA) ? MFS_DATA_PREFIX : 16u;

            if (hdr->payload_size < need) {
                return MFS_OK;           /* 坏记录：不影响其它候选 */
            }
            err = mfs_rec_read_raw(fs, rec_addr, hdr, 0u, buf, need);
            if (err != MFS_OK) {
                return err;
            }
            r_epoch = mfs_get_u32(&buf[12]);
            r_chunk = mfs_get_u32(&buf[4]);
            if (hdr->type == MFS_REC_DATA_DELETE) {
                r_count = mfs_get_u32(&buf[8]);
            }
            prefix_ok = true;
        }
        if (r_epoch != c->epoch || hdr->sequence <= c->seq) {
            continue;
        }
        if (hdr->type == MFS_REC_DATA) {
            if (r_chunk == c->chunk) {
                c->live = 0u;
            }
        } else if (c->chunk >= r_chunk && c->chunk - r_chunk < r_count) {
            c->live = 0u;
        }
    }
    return MFS_OK;
}

/* 读取指定地址的 Inode 记录并解析（Payload CRC 完整校验） */
static int gc_read_inode(mfs_t *fs, uint32_t rec_addr, mfs_ii_t *info)
{
    mfs_rec_hdr_t hdr;
    uint8_t buf[MFS_GC_INODE_BUF];
    int err;

    err = mfs_rec_read(fs, rec_addr, &hdr, buf, sizeof(buf), NULL);
    if (err != MFS_OK) {
        return err;
    }
    return mfs_inode_parse(buf, hdr.payload_size, info);
}

/* 根据单遍扫描结果确定候选记录是否有效，并累计有效字节数 */
static int gc_resolve(mfs_t *fs, gc_block_t *b, gc_eval_t *e,
                      uint32_t *live_out)
{
    uint32_t live = 0u;
    uint32_t i;
    int err;

    for (i = 0u; i < e->count; i++) {
        gc_rec_t *c = &e->recs[i];

        if (c->live == 0u) {
            continue;
        }
        if (c->type == MFS_REC_DATA) {
            mfs_ii_t info;

            if (e->inode_seq[i] == 0u ||
                e->inode_type[i] == MFS_REC_INODE_DELETE) {
                c->live = 0u;           /* 文件已删除 */
                continue;
            }
            err = gc_read_inode(fs, e->inode_addr[i], &info);
            if (err == MFS_ERR_NOENT || err == MFS_ERR_CRC ||
                err == MFS_ERR_CORRUPT) {
                c->live = 0u;           /* 最新 Inode 记录损坏：保守回收 */
                continue;
            }
            if (err != MFS_OK) {
                return err;
            }
            if (c->epoch != info.epoch || info.size == 0u ||
                c->chunk > (info.size - 1u) / MFS_CHUNK_SIZE) {
                c->live = 0u;           /* 已截断/换代的残留 Chunk */
                continue;
            }
        } else if (gc_is_inode_rec(c->type)) {
            if (c->seq != e->inode_seq[i]) {
                c->live = 0u;           /* 已有更新版本 */
                continue;
            }
            if (e->inode_type[i] == MFS_REC_INODE_DELETE) {
                c->live = (c->type == MFS_REC_INODE_DELETE) ? 1u : 0u;
            } else {
                c->live = (c->type == MFS_REC_INODE_DELETE) ? 0u : 1u;
            }
        }
        if (c->live != 0u) {
            live += mfs_rec_total_size(fs, c->addr - b->base,
                                         c->payload_size);
        }
    }
    *live_out = live;
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
        err = mfs_rec_read_raw(fs, rec_addr, hdr, 0u, prefix,
                                 sizeof(prefix));
        if (err != MFS_OK) {
            return err;
        }
        err = mfs_rec_read_raw(fs, rec_addr, hdr, MFS_DATA_PREFIX,
                                 fs->workbuf, data_size);
        if (err != MFS_OK) {
            return err;
        }
        return mfs_rec_write(fs, hdr->type, hdr->object_id,
                               prefix, sizeof(prefix),
                               fs->workbuf, data_size);
    }

    if (size > fs->workbuf_size) {
        return MFS_ERR_CORRUPT;
    }
    err = mfs_rec_read_raw(fs, rec_addr, hdr, 0u, fs->workbuf, size);
    if (err != MFS_OK) {
        return err;
    }
    return mfs_rec_write(fs, hdr->type, hdr->object_id,
                           fs->workbuf, size, NULL, 0u);
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

int mfs_gc_run(mfs_t *fs, uint8_t force)
{
    static gc_block_t s_blk;
    static gc_eval_t  s_eval;
    uint32_t usable;
    uint32_t min_seq = 0u;
    uint32_t victim = MFS_BLOCK_NONE;
    uint32_t victim_erase = 0u;
    uint32_t attempt;
    uint32_t i;
    uint32_t last = 0u;
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

    /*
     * 1. 选择牺牲块：按块头 sequence 从旧到新逐个尝试，
     *    最新的空闲块（刚被 GC 擦除）sequence 最大，不会被优先选中。
     */
    for (attempt = 0u; attempt < MFS_GC_TRY_BLOCKS; attempt++) {
        uint32_t pick = MFS_BLOCK_NONE;
        uint32_t pick_seq = 0xFFFFFFFFu;
        uint32_t pick_erase = 0u;
        uint32_t block;
        uint32_t live = 0u;
        uint32_t ratio;

        for (block = 0u; block < fs->block_count; block++) {
            mfs_blk_hdr_t bh;

            if (block == fs->active_block || block == fs->gc_block) {
                continue;
            }
            if (mfs_blk_read_hdr(fs, block, &bh) != MFS_OK) {
                continue;               /* 未初始化/无效块头 */
            }
            if (bh.state == MFS_BLK_STATE_BAD) {
                continue;
            }
            if (bh.sequence > min_seq && bh.sequence < pick_seq) {
                pick = block;
                pick_seq = bh.sequence;
                pick_erase = bh.erase_count;
            }
        }
        if (pick == MFS_BLOCK_NONE) {
            break;                      /* 没有更多候选块 */
        }
        min_seq = pick_seq;

        memset(&s_blk, 0, sizeof(s_blk));
        s_blk.base = mfs_blk_addr(fs, pick);
        err = mfs_scan_block_fast(fs, pick, gc_collect_cb, &s_blk, &last);
        if (s_blk.overflow != 0u) {
            continue;                   /* 记录过多：本轮跳过该块 */
        }
        if (err != MFS_OK && err != MFS_ERR_NOENT && err != MFS_ERR_CRC) {
            return err;
        }
        if (s_blk.count == 0u) {
            uint8_t empty = 0u;

            if (mfs_blk_is_empty(fs, pick, &empty) != MFS_OK) {
                return MFS_ERR_IO;
            }
            if (empty != 0u) {
                continue;               /* 真正空块 */
            }
            live = 0u;                  /* 只有坏记录头：整块可回收 */
        } else {
            memset(&s_eval, 0, sizeof(s_eval));
            s_eval.recs = s_blk.recs;
            s_eval.count = s_blk.count;
            err = mfs_scan_all_fast(fs, gc_eval_cb, &s_eval);
            if (err != MFS_OK) {
                return err;
            }
            err = gc_resolve(fs, &s_blk, &s_eval, &live);
            if (err != MFS_OK) {
                return err;
            }
        }

        ratio = (usable != 0u)
                    ? (uint32_t)(((uint64_t)live * 100u) / usable)
                    : 100u;
        if (ratio >= 100u) {
            continue;                   /* 没有可回收空间 */
        }
        if (!force && ratio >= MFS_GC_LIVE_MAX) {
            continue;                   /* 有效数据太多，不值得搬 */
        }
        if (force && ratio > MFS_GC_FORCE_LIVE_MAX) {
            continue;                   /* 回收收益太小，留待真正需要时再搬 */
        }
        victim = pick;
        victim_erase = pick_erase;
        break;
    }

    if (victim == MFS_BLOCK_NONE) {
        return MFS_ERR_NOSPC;           /* 没有可回收的块（全部有效） */
    }

    fs->gc_running = 1u;
    fs->gc_block = victim;

    /* 2. 只复制评估为有效的记录 */
    for (i = 0u; i < s_blk.count; i++) {
        mfs_rec_hdr_t hdr;

        if (s_blk.recs[i].live == 0u) {
            continue;
        }
        err = mfs_rec_read_fast(fs, s_blk.recs[i].addr, &hdr, NULL);
        if (err != MFS_OK) {
            fs->gc_block = MFS_BLOCK_NONE;
            fs->gc_running = 0u;
            return err;
        }
        err = gc_copy_record(fs, &hdr, s_blk.recs[i].addr);
        if (err != MFS_OK) {
            fs->gc_block = MFS_BLOCK_NONE;
            fs->gc_running = 0u;
            return err;
        }
    }
    err = mfs_dev_sync(fs);
    if (err != MFS_OK) {
        fs->gc_block = MFS_BLOCK_NONE;
        fs->gc_running = 0u;
        return err;
    }

    /* 3. 擦除牺牲块并写 FREE 头（沿用原擦除次数 + 1） */
    err = mfs_dev_erase(fs, mfs_blk_addr(fs, victim), fs->block_size);
    if (err != MFS_OK) {
        /* 尽量持久化坏块标记；原数据已被复制，文件系统仍可用 */
        (void)mfs_blk_mark_bad(fs, victim, victim_erase + 1u);
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

    err = mfs_blk_write_hdr(fs, victim, fs->seq++, victim_erase + 1u,
                              MFS_BLK_STATE_FREE);
    fs->gc_block = MFS_BLOCK_NONE;
    fs->gc_running = 0u;
    return err;
}
