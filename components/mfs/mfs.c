/*
 * mfs.c - 生命周期、Superblock、Mount/Format 与空间统计
 * ============================================================================
 *
 * Superblock 格式（小端，24B，位于分区起始的两个擦除块）：
 *
 *   +0  magic       4B   "ROFS"
 *   +4  version     4B   (MAJOR << 16) | MINOR
 *   +8  block_size  4B   与设备 erase_size 一致
 *   +12 block_count 4B   数据池块数（不含 A/B）
 *   +16 generation  4B   格式化代次
 *   +20 crc         4B   前 20B 的 CRC32
 *
 * A、B 两份内容相同，挂载时选择 generation 更大的有效副本。
 *
 * 挂载流程：
 *   1. 读取并校验 A/B，选定版本；
 *   2. 逐块读取 Block Header，统计空闲块；
 *   3. 扫描每个有效块中的 Record，恢复：
 *        - 最大 sequence（下一条记录序号）
 *        - 最大 Inode 编号（下一个 Inode）
 *        - 最大 tx_id
 *        - 每块最后一条有效记录的偏移；
 *   4. 选择 sequence 最大且有剩余空间的活动块；
 *   5. 校验根 Inode 存在且为目录。
 *
 * 格式化流程：
 *   1. 读取旧 Superblock 以递增 generation；
 *   2. 擦除所有数据块与 A/B；
 *   3. 写 Block 0 Header；
 *   4. 创建根目录 Inode；
 *   5. 最后写 Superblock（写 SB 成功即代表格式化完成）。
 */

#include "mfs_internal.h"

/* ============================================================================
 * 锁
 * ==========================================================================*/

/* 若配置了 lock 回调则加锁；内部函数不再重复加锁 */
void mfs__lock(mfs_t *fs)
{
    if (fs != NULL && fs->cfg.lock != NULL) {
        fs->cfg.lock(fs->cfg.mutex_ctx);
    }
}

/* 与 mfs__lock 对应 */
void mfs__unlock(mfs_t *fs)
{
    if (fs != NULL && fs->cfg.unlock != NULL) {
        fs->cfg.unlock(fs->cfg.mutex_ctx);
    }
}

/* ============================================================================
 * Superblock 读写
 * ==========================================================================*/

/* A/B 两个 Superblock 的起始地址 */
static uint32_t sb_addr(mfs_t *fs, uint32_t slot)
{
    return fs->dev.start_addr + slot * fs->block_size;
}

/*
 * 读取并校验一个 Superblock。
 * 返回 MFS_OK / MFS_ERR_FORMAT / MFS_ERR_VERSION / MFS_ERR_CRC / IO。
 */
static int sb_read(mfs_t *fs, uint32_t slot, mfs_sb_t *sb)
{
    uint8_t buf[MFS_SB_SIZE];
    int err;

    err = mfs__read(fs, sb_addr(fs, slot), buf, sizeof(buf));
    if (err != MFS_OK) {
        return err;
    }

    sb->magic = mfs__get_u32(&buf[0]);
    sb->version = mfs__get_u32(&buf[4]);
    sb->block_size = mfs__get_u32(&buf[8]);
    sb->block_count = mfs__get_u32(&buf[12]);
    sb->generation = mfs__get_u32(&buf[16]);
    sb->crc = mfs__get_u32(&buf[20]);

    if (sb->magic != MFS_SB_MAGIC) {
        return MFS_ERR_FORMAT;
    }
    if (sb->version != ((MFS_VERSION_MAJOR << 16) | MFS_VERSION_MINOR)) {
        return MFS_ERR_VERSION;
    }
    if (mfs__crc32(buf, 20u) != sb->crc) {
        return MFS_ERR_CRC;
    }
    /* 分区几何必须与当前设备配置一致 */
    if (sb->block_size != fs->block_size ||
        sb->block_count != fs->block_count) {
        return MFS_ERR_FORMAT;
    }
    return MFS_OK;
}

/*
 * 依次写入 A、B 两份 Superblock，每份写完立即 sync。
 * 两份都成功才返回 OK；若中途失败，旧的一份仍可能可用。
 */
static int sb_write(mfs_t *fs)
{
    uint8_t buf[MFS_SB_SIZE];
    uint32_t slot;
    int err;

    mfs__put_u32(&buf[0], MFS_SB_MAGIC);
    mfs__put_u32(&buf[4], (MFS_VERSION_MAJOR << 16) | MFS_VERSION_MINOR);
    mfs__put_u32(&buf[8], fs->block_size);
    mfs__put_u32(&buf[12], fs->block_count);
    mfs__put_u32(&buf[16], fs->generation);
    mfs__put_u32(&buf[20], mfs__crc32(buf, 20u));

    for (slot = 0; slot < 2u; slot++) {
        err = mfs__prog(fs, sb_addr(fs, slot), buf, sizeof(buf));
        if (err != MFS_OK) {
            return err;
        }
        err = mfs__sync(fs);
        if (err != MFS_OK) {
            return err;
        }
    }
    return MFS_OK;
}

/* ============================================================================
 * Init
 * ==========================================================================*/

/*
 * 复位运行期状态并清空所有句柄。
 * 只改内存，不访问 Flash。
 */
static void fs_reset_runtime(mfs_t *fs)
{
    uint32_t i;

    fs->seq = 1u;
    fs->next_inode = 1u;
    fs->next_tx = 1u;
    fs->active_block = MFS_BLOCK_NONE;
    fs->active_off = 0u;
    fs->erase_count = 0u;
    fs->free_blocks = 0u;
    fs->bad_blocks = 0u;
    fs->used_bytes = 0u;
    fs->alloc_hint = 0u;
    fs->gc_block = MFS_BLOCK_NONE;
    fs->gc_running = 0u;

    for (i = 0; i < MFS_MAX_OPEN_FILES; i++) {
        fs->files[i].used = 0u;
        fs->files[i].fs = fs;
    }
    for (i = 0; i < MFS_MAX_OPEN_DIRS; i++) {
        fs->dirs[i].used = 0u;
        fs->dirs[i].fs = fs;
    }
}

/*
 * 初始化文件系统实例。
 *
 * 校验内容：
 *   - dev 必须提供 read/program/erase；
 *   - workbuf 至少 MFS_WORKBUF_SIZE；
 *   - erase_size >= 256，分区至少 3 个块且为 erase_size 整数倍。
 *
 * 成功后只完成内存初始化，尚未挂载，不访问 Flash。
 */
int mfs_init(mfs_t *fs, const mfs_blockdev_t *dev, const mfs_config_t *config)
{
    uint32_t blocks;

    if (fs == NULL || dev == NULL || config == NULL) {
        return MFS_ERR_PARAM;
    }
    if (dev->read == NULL || dev->program == NULL || dev->erase == NULL) {
        return MFS_ERR_PARAM;
    }
    if (config->workbuf == NULL ||
        config->workbuf_size < MFS_WORKBUF_SIZE) {
        return MFS_ERR_PARAM;
    }
    if (dev->erase_size < 256u || dev->total_size < 3u * dev->erase_size) {
        return MFS_ERR_PARAM;
    }
    if (dev->total_size % dev->erase_size != 0u) {
        return MFS_ERR_PARAM;
    }

    memset(fs, 0, sizeof(*fs));
    fs->dev = *dev;
    fs->cfg = *config;
    fs->workbuf = config->workbuf;
    fs->workbuf_size = config->workbuf_size;

    if (fs->dev.program_size == 0u) {
        fs->dev.program_size = 1u;
    }
    if (fs->dev.read_size == 0u) {
        fs->dev.read_size = 1u;
    }

    fs->block_size = dev->erase_size;
    blocks = dev->total_size / dev->erase_size;
    fs->block_count = blocks - 2u;        /* 前两块留给 Superblock A/B */
    fs->pool_start = dev->start_addr + 2u * fs->block_size;

    /* 句柄数不能超过编译期数组大小 */
    if (fs->cfg.max_open_files == 0u ||
        fs->cfg.max_open_files > MFS_MAX_OPEN_FILES) {
        fs->cfg.max_open_files = MFS_MAX_OPEN_FILES;
    }
    if (fs->cfg.max_open_dirs == 0u ||
        fs->cfg.max_open_dirs > MFS_MAX_OPEN_DIRS) {
        fs->cfg.max_open_dirs = MFS_MAX_OPEN_DIRS;
    }

    fs->readonly = config->readonly ? 1u : 0u;
    fs->mounted = 0u;
    fs->generation = 1u;
    fs_reset_runtime(fs);

    return MFS_OK;
}

/* ============================================================================
 * Format
 * ==========================================================================*/

/*
 * 格式化分区。
 *
 * 顺序很重要：
 *   - 先擦除数据池和 Superblock 区；
 *   - 再写 Block 0 和根 Inode；
 *   - 最后写 Superblock。
 * 若在写 Superblock 之前掉电，旧 Superblock 可能指向已擦除区域，
 * 挂载会失败，调用者重新格式化即可。
 *
 * 格式化成功后 fs 处于已挂载状态。
 */
int mfs_format(mfs_t *fs)
{
    mfs_sb_t sb;
    mfs_blk_hdr_t bh;
    mfs_ii_t root;
    uint32_t block;
    uint32_t old_count;
    int err;

    if (fs == NULL) {
        return MFS_ERR_PARAM;
    }
    if (fs->readonly) {
        return MFS_ERR_READONLY;
    }

    mfs__lock(fs);

    /* 尽量在旧代次基础上递增，便于区分多次格式化 */
    if (sb_read(fs, 0u, &sb) == MFS_OK ||
        sb_read(fs, 1u, &sb) == MFS_OK) {
        fs->generation = sb.generation + 1u;
    } else {
        fs->generation = 1u;
    }

    fs_reset_runtime(fs);
    fs->mounted = 1u;                    /* 允许内部写记录 */

    /*
     * 初始化所有数据块：
     *   - 已知 BAD 块保留，不参与分配；
     *   - 其余块擦除后写 FREE 头，擦除次数在旧值上 +1；
     *   - 擦除失败则尝试标记为 BAD 并跳过。
     */
    for (block = 0; block < fs->block_count; block++) {
        old_count = 1u;
        if (mfs__blk_read_hdr(fs, block, &bh) == MFS_OK) {
            old_count = bh.erase_count + 1u;
            if (bh.state == MFS_BLK_STATE_BAD) {
                fs->bad_blocks++;
                continue;                /* 保留坏块标记 */
            }
        }

        err = mfs__erase(fs, mfs__blk_addr(fs, block), fs->block_size);
        if (err != MFS_OK) {
            (void)mfs__blk_mark_bad(fs, block, old_count);
            fs->bad_blocks++;
            continue;
        }
        err = mfs__blk_write_hdr(fs, block, fs->seq++, old_count,
                                  MFS_BLK_STATE_FREE);
        if (err != MFS_OK) {
            fs->mounted = 0u;
            mfs__unlock(fs);
            return err;
        }
        fs->free_blocks++;
    }

    if (fs->free_blocks == 0u) {
        fs->mounted = 0u;
        mfs__unlock(fs);
        return MFS_ERR_NOSPC;
    }

    /* 擦除 Superblock A/B */
    for (block = 0; block < 2u; block++) {
        err = mfs__erase(fs, sb_addr(fs, block), fs->block_size);
        if (err != MFS_OK) {
            fs->mounted = 0u;
            mfs__unlock(fs);
            return err;
        }
    }

    /* 选择一个空闲块作为初始活动块（按最低擦除次数） */
    err = mfs__blk_alloc(fs);
    if (err != MFS_OK) {
        fs->mounted = 0u;
        mfs__unlock(fs);
        return err;
    }

    /* 创建根目录 Inode：next_inode 从 1 开始，因此根固定为 1 */
    fs->next_inode = 1u;
    err = mfs__inode_create(fs, MFS_INVALID_INODE, "", MFS_TYPE_DIR, &root);
    if (err != MFS_OK) {
        fs->mounted = 0u;
        mfs__unlock(fs);
        return err;
    }

    /* 最后写 Superblock，成功即格式化完成 */
    err = sb_write(fs);
    if (err != MFS_OK) {
        fs->mounted = 0u;
        mfs__unlock(fs);
        return err;
    }

    mfs__unlock(fs);
    return MFS_OK;
}

/* ============================================================================
 * Mount
 * ==========================================================================*/

typedef struct {
    uint32_t max_tx;      /* 扫描到的最大事务号 */
} mount_ctx_t;

/*
 * 挂载扫描回调。
 * 对每条有效记录更新全局 sequence、最大 Inode 编号和最大事务号。
 * 不保存 Inode 内容，保持 RAM 恒定。
 */
static int mount_cb(mfs_t *fs, const mfs_rec_hdr_t *hdr,
                    uint32_t rec_addr, void *ctx)
{
    mount_ctx_t *m = (mount_ctx_t *)ctx;

    (void)rec_addr;
    if (hdr->sequence >= fs->seq) {
        fs->seq = hdr->sequence + 1u;
    }
    if (hdr->object_id >= fs->next_inode) {
        fs->next_inode = hdr->object_id + 1u;
    }
    if (hdr->tx_id > m->max_tx) {
        m->max_tx = hdr->tx_id;
    }
    return MFS_OK;
}

/*
 * 挂载文件系统。
 *
 * 不依赖内存中的任何旧状态，可重复调用：
 *   - 重新扫描全部日志；
 *   - 恢复活动块与全局计数；
 *   - 清空残留句柄。
 */
int mfs_mount(mfs_t *fs)
{
    mfs_sb_t sb_a;
    mfs_sb_t sb_b;
    mfs_sb_t sb;
    mfs_blk_hdr_t bh;
    mfs_ii_t root;
    mount_ctx_t m;
    uint32_t block;
    uint32_t last;
    uint32_t best_block = MFS_BLOCK_NONE;
    uint32_t best_seq = 0u;
    int err_a;
    int err_b;
    int err;

    if (fs == NULL) {
        return MFS_ERR_PARAM;
    }

    mfs__lock(fs);
    fs_reset_runtime(fs);

    /* 1. 选择 generation 更大的有效 Superblock */
    err_a = sb_read(fs, 0u, &sb_a);
    err_b = sb_read(fs, 1u, &sb_b);

    if (err_a == MFS_OK && err_b == MFS_OK) {
        sb = (sb_a.generation >= sb_b.generation) ? sb_a : sb_b;
    } else if (err_a == MFS_OK) {
        sb = sb_a;
    } else if (err_b == MFS_OK) {
        sb = sb_b;
    } else {
        mfs__unlock(fs);
        return (err_a == MFS_ERR_VERSION || err_b == MFS_ERR_VERSION)
                   ? MFS_ERR_VERSION : MFS_ERR_FORMAT;
    }
    fs->generation = sb.generation;

    memset(&m, 0, sizeof(m));

    /* 2. 逐块扫描，恢复日志状态并挑选活动块 */
    for (block = 0; block < fs->block_count; block++) {
        err = mfs__blk_read_hdr(fs, block, &bh);
        if (err == MFS_ERR_NOENT || err == MFS_ERR_CRC) {
            fs->free_blocks++;           /* 未初始化/已擦除：空闲块 */
            continue;
        }
        if (err != MFS_OK) {
            mfs__unlock(fs);
            return err;
        }
        if (bh.state == MFS_BLK_STATE_BAD) {
            fs->bad_blocks++;
            continue;
        }

        err = mfs__scan_block_fast(fs, block, mount_cb, &m, &last);
        if (err != MFS_OK) {
            mfs__unlock(fs);
            return err;
        }

        /* 没有任何有效记录：区分真正空块与写坏的块 */
        if (last == MFS_BLK_HDR_SIZE) {
            uint8_t empty = 0u;

            err = mfs__blk_is_empty(fs, block, &empty);
            if (err != MFS_OK) {
                mfs__unlock(fs);
                return err;
            }
            if (empty != 0u) {
                fs->free_blocks++;
            }
            continue;
        }

        /* 有记录且还有空间：作为候选活动块，sequence 越大越新 */
        if (last + MFS_REC_HDR_SIZE <= fs->block_size &&
            bh.sequence >= best_seq) {
            best_seq = bh.sequence;
            best_block = block;
            fs->active_off = last;
        }
    }

    fs->next_tx = m.max_tx + 1u;
    if (fs->next_inode < 2u) {
        fs->next_inode = 2u;             /* 1 固定为根 */
    }
    if (best_block != MFS_BLOCK_NONE) {
        fs->active_block = best_block;
    } else {
        fs->active_block = MFS_BLOCK_NONE;
        fs->active_off = 0u;
    }

    /* 3. 根目录必须存在且是目录 */
    err = mfs__inode_find(fs, MFS_ROOT_INODE, &root);
    if (err != MFS_OK) {
        mfs__unlock(fs);
        return (err == MFS_ERR_NOENT) ? MFS_ERR_FORMAT : err;
    }
    if (root.type != MFS_TYPE_DIR) {
        mfs__unlock(fs);
        return MFS_ERR_CORRUPT;
    }

    fs->mounted = 1u;
    mfs__unlock(fs);
    return MFS_OK;
}

/* ============================================================================
 * Unmount / Sync
 * ==========================================================================*/

/*
 * 卸载：同步介质、清空句柄、标记未挂载。
 * 不检查是否有文件仍打开（句柄会直接失效）。
 */
int mfs_unmount(mfs_t *fs)
{
    uint32_t i;

    if (fs == NULL) {
        return MFS_ERR_PARAM;
    }

    mfs__lock(fs);
    if (fs->mounted) {
        (void)mfs__sync(fs);
    }
    for (i = 0; i < MFS_MAX_OPEN_FILES; i++) {
        fs->files[i].used = 0u;
    }
    for (i = 0; i < MFS_MAX_OPEN_DIRS; i++) {
        fs->dirs[i].used = 0u;
    }
    fs->mounted = 0u;
    mfs__unlock(fs);
    return MFS_OK;
}

/* 同步底层介质；未挂载返回 MFS_ERR_STATE */
int mfs_sync(mfs_t *fs)
{
    int err;

    if (fs == NULL || !fs->mounted) {
        return MFS_ERR_STATE;
    }
    mfs__lock(fs);
    err = mfs__sync(fs);
    mfs__unlock(fs);
    return err;
}

/* ============================================================================
 * 统计
 * ==========================================================================*/

/*
 * 空间统计。
 * free_bytes = 完全空闲块 + 活动块尾部剩余；
 * 已分配但只用了部分空间的旧块，其余部分不计入 free（由 GC 回收）；
 * free_blocks 只统计完全空闲块，bad_blocks 不计入可用容量。
 */
int mfs_statfs(mfs_t *fs, mfs_statfs_t *stat)
{
    uint32_t total;
    uint32_t free_bytes;

    if (fs == NULL || stat == NULL || !fs->mounted) {
        return MFS_ERR_STATE;
    }

    mfs__lock(fs);
    total = fs->block_count * fs->block_size;
    free_bytes = fs->free_blocks * fs->block_size;
    if (fs->active_block != MFS_BLOCK_NONE &&
        fs->active_off < fs->block_size) {
        free_bytes += fs->block_size - fs->active_off;
    }

    stat->total_bytes = total;
    stat->free_bytes = free_bytes;
    stat->used_bytes = total - free_bytes -
                       fs->bad_blocks * fs->block_size;
    stat->total_blocks = fs->block_count;
    stat->free_blocks = fs->free_blocks;
    stat->bad_blocks = fs->bad_blocks;
    stat->block_size = fs->block_size;
    mfs__unlock(fs);
    return MFS_OK;
}

/* 剩余空间快捷接口 */
int mfs_free_space(mfs_t *fs, uint32_t *size)
{
    mfs_statfs_t stat;
    int err;

    if (size == NULL) {
        return MFS_ERR_PARAM;
    }
    err = mfs_statfs(fs, &stat);
    if (err != MFS_OK) {
        return err;
    }
    *size = stat.free_bytes;
    return MFS_OK;
}

/* 总空间快捷接口 */
int mfs_total_space(mfs_t *fs, uint32_t *size)
{
    mfs_statfs_t stat;
    int err;

    if (size == NULL) {
        return MFS_ERR_PARAM;
    }
    err = mfs_statfs(fs, &stat);
    if (err != MFS_OK) {
        return err;
    }
    *size = stat.total_bytes;
    return MFS_OK;
}

/*
 * 基础自检：
 *   - Superblock A 或 B 至少一个有效；
 *   - 扫描所有块中的有效记录（CRC 由扫描过程校验）。
 * 注意：块尾未提交记录属于正常掉电残留，不算错误。
 */
int mfs_check(mfs_t *fs)
{
    mfs_sb_t sb;
    int err;

    if (fs == NULL || !fs->mounted) {
        return MFS_ERR_STATE;
    }

    mfs__lock(fs);
    err = sb_read(fs, 0u, &sb);
    if (err != MFS_OK) {
        err = sb_read(fs, 1u, &sb);
    }
    if (err == MFS_OK) {
        err = mfs__scan_all(fs, NULL, NULL);
    }
    mfs__unlock(fs);
    return err;
}

