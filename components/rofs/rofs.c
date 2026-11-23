/*
 * rofs.c - 生命周期、Superblock、Mount/Format、统计与维护接口
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

#include "rofs_internal.h"

/* ============================================================================
 * 锁
 * ==========================================================================*/

/* 若配置了 lock 回调则加锁；内部函数不再重复加锁 */
void rofs__lock(rofs_t *fs)
{
    if (fs != NULL && fs->cfg.lock != NULL) {
        fs->cfg.lock(fs->cfg.mutex_ctx);
    }
}

/* 与 rofs__lock 对应 */
void rofs__unlock(rofs_t *fs)
{
    if (fs != NULL && fs->cfg.unlock != NULL) {
        fs->cfg.unlock(fs->cfg.mutex_ctx);
    }
}

/* ============================================================================
 * Superblock 读写
 * ==========================================================================*/

/* A/B 两个 Superblock 的起始地址 */
static uint32_t sb_addr(rofs_t *fs, uint32_t slot)
{
    return fs->dev.start_addr + slot * fs->block_size;
}

/*
 * 读取并校验一个 Superblock。
 * 返回 ROFS_OK / ROFS_ERR_FORMAT / ROFS_ERR_VERSION / ROFS_ERR_CRC / IO。
 */
static int sb_read(rofs_t *fs, uint32_t slot, rofs_sb_t *sb)
{
    uint8_t buf[ROFS_SB_SIZE];
    int err;

    err = rofs__read(fs, sb_addr(fs, slot), buf, sizeof(buf));
    if (err != ROFS_OK) {
        return err;
    }

    sb->magic = rofs__get_u32(&buf[0]);
    sb->version = rofs__get_u32(&buf[4]);
    sb->block_size = rofs__get_u32(&buf[8]);
    sb->block_count = rofs__get_u32(&buf[12]);
    sb->generation = rofs__get_u32(&buf[16]);
    sb->crc = rofs__get_u32(&buf[20]);

    if (sb->magic != ROFS_SB_MAGIC) {
        return ROFS_ERR_FORMAT;
    }
    if (sb->version != ((ROFS_VERSION_MAJOR << 16) | ROFS_VERSION_MINOR)) {
        return ROFS_ERR_VERSION;
    }
    if (rofs__crc32(buf, 20u) != sb->crc) {
        return ROFS_ERR_CRC;
    }
    /* 分区几何必须与当前设备配置一致 */
    if (sb->block_size != fs->block_size ||
        sb->block_count != fs->block_count) {
        return ROFS_ERR_FORMAT;
    }
    return ROFS_OK;
}

/*
 * 依次写入 A、B 两份 Superblock，每份写完立即 sync。
 * 两份都成功才返回 OK；若中途失败，旧的一份仍可能可用。
 */
static int sb_write(rofs_t *fs)
{
    uint8_t buf[ROFS_SB_SIZE];
    uint32_t slot;
    int err;

    rofs__put_u32(&buf[0], ROFS_SB_MAGIC);
    rofs__put_u32(&buf[4], (ROFS_VERSION_MAJOR << 16) | ROFS_VERSION_MINOR);
    rofs__put_u32(&buf[8], fs->block_size);
    rofs__put_u32(&buf[12], fs->block_count);
    rofs__put_u32(&buf[16], fs->generation);
    rofs__put_u32(&buf[20], rofs__crc32(buf, 20u));

    for (slot = 0; slot < 2u; slot++) {
        err = rofs__prog(fs, sb_addr(fs, slot), buf, sizeof(buf));
        if (err != ROFS_OK) {
            return err;
        }
        err = rofs__sync(fs);
        if (err != ROFS_OK) {
            return err;
        }
    }
    return ROFS_OK;
}

/* ============================================================================
 * Init
 * ==========================================================================*/

/*
 * 复位运行期状态并清空所有句柄。
 * 只改内存，不访问 Flash。
 */
static void fs_reset_runtime(rofs_t *fs)
{
    uint32_t i;

    fs->seq = 1u;
    fs->next_inode = 1u;
    fs->next_tx = 1u;
    fs->active_block = ROFS_BLOCK_NONE;
    fs->active_off = 0u;
    fs->erase_count = 0u;
    fs->free_blocks = 0u;
    fs->used_bytes = 0u;

    for (i = 0; i < ROFS_MAX_OPEN_FILES; i++) {
        fs->files[i].used = 0u;
        fs->files[i].fs = fs;
    }
    for (i = 0; i < ROFS_MAX_OPEN_DIRS; i++) {
        fs->dirs[i].used = 0u;
        fs->dirs[i].fs = fs;
    }
}

/*
 * 初始化文件系统实例。
 *
 * 校验内容：
 *   - dev 必须提供 read/program/erase；
 *   - workbuf 至少 ROFS_WORKBUF_SIZE；
 *   - erase_size >= 256，分区至少 3 个块且为 erase_size 整数倍。
 *
 * 成功后只完成内存初始化，尚未挂载，不访问 Flash。
 */
int rofs_init(rofs_t *fs, const rofs_blockdev_t *dev, const rofs_config_t *config)
{
    uint32_t blocks;

    if (fs == NULL || dev == NULL || config == NULL) {
        return ROFS_ERR_PARAM;
    }
    if (dev->read == NULL || dev->program == NULL || dev->erase == NULL) {
        return ROFS_ERR_PARAM;
    }
    if (config->workbuf == NULL ||
        config->workbuf_size < ROFS_WORKBUF_SIZE) {
        return ROFS_ERR_PARAM;
    }
    if (dev->erase_size < 256u || dev->total_size < 3u * dev->erase_size) {
        return ROFS_ERR_PARAM;
    }
    if (dev->total_size % dev->erase_size != 0u) {
        return ROFS_ERR_PARAM;
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
        fs->cfg.max_open_files > ROFS_MAX_OPEN_FILES) {
        fs->cfg.max_open_files = ROFS_MAX_OPEN_FILES;
    }
    if (fs->cfg.max_open_dirs == 0u ||
        fs->cfg.max_open_dirs > ROFS_MAX_OPEN_DIRS) {
        fs->cfg.max_open_dirs = ROFS_MAX_OPEN_DIRS;
    }

    fs->readonly = config->readonly ? 1u : 0u;
    fs->mounted = 0u;
    fs->generation = 1u;
    fs_reset_runtime(fs);

    return ROFS_OK;
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
int rofs_format(rofs_t *fs)
{
    rofs_sb_t sb;
    rofs_ii_t root;
    uint32_t block;
    int err;

    if (fs == NULL) {
        return ROFS_ERR_PARAM;
    }
    if (fs->readonly) {
        return ROFS_ERR_READONLY;
    }

    rofs__lock(fs);

    /* 尽量在旧代次基础上递增，便于区分多次格式化 */
    if (sb_read(fs, 0u, &sb) == ROFS_OK ||
        sb_read(fs, 1u, &sb) == ROFS_OK) {
        fs->generation = sb.generation + 1u;
    } else {
        fs->generation = 1u;
    }

    fs_reset_runtime(fs);
    fs->mounted = 1u;                    /* 允许内部写记录 */

    /* 擦除全部数据块，确保没有残留记录 */
    for (block = 0; block < fs->block_count; block++) {
        err = rofs__erase(fs, rofs__blk_addr(fs, block), fs->block_size);
        if (err != ROFS_OK) {
            fs->mounted = 0u;
            rofs__unlock(fs);
            return err;
        }
        fs->free_blocks++;
    }

    /* 擦除 Superblock A/B */
    for (block = 0; block < 2u; block++) {
        err = rofs__erase(fs, sb_addr(fs, block), fs->block_size);
        if (err != ROFS_OK) {
            fs->mounted = 0u;
            rofs__unlock(fs);
            return err;
        }
    }

    /* 数据池第 0 块作为初始活动块 */
    err = rofs__blk_write_hdr(fs, 0u, fs->seq++, 1u);
    if (err != ROFS_OK) {
        fs->mounted = 0u;
        rofs__unlock(fs);
        return err;
    }
    fs->active_block = 0u;
    fs->active_off = ROFS_BLK_HDR_SIZE;
    fs->free_blocks--;
    fs->used_bytes += fs->block_size;

    /* 创建根目录 Inode：next_inode 从 1 开始，因此根固定为 1 */
    fs->next_inode = 1u;
    err = rofs__inode_create(fs, ROFS_INVALID_INODE, "", ROFS_TYPE_DIR, &root);
    if (err != ROFS_OK) {
        fs->mounted = 0u;
        rofs__unlock(fs);
        return err;
    }

    /* 最后写 Superblock，成功即格式化完成 */
    err = sb_write(fs);
    if (err != ROFS_OK) {
        fs->mounted = 0u;
        rofs__unlock(fs);
        return err;
    }

    rofs__unlock(fs);
    return ROFS_OK;
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
static int mount_cb(rofs_t *fs, const rofs_rec_hdr_t *hdr,
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
    return ROFS_OK;
}

/*
 * 挂载文件系统。
 *
 * 不依赖内存中的任何旧状态，可重复调用：
 *   - 重新扫描全部日志；
 *   - 恢复活动块与全局计数；
 *   - 清空残留句柄。
 */
int rofs_mount(rofs_t *fs)
{
    rofs_sb_t sb_a;
    rofs_sb_t sb_b;
    rofs_sb_t sb;
    rofs_blk_hdr_t bh;
    rofs_ii_t root;
    mount_ctx_t m;
    uint32_t block;
    uint32_t last;
    uint32_t best_block = ROFS_BLOCK_NONE;
    uint32_t best_seq = 0u;
    int err_a;
    int err_b;
    int err;

    if (fs == NULL) {
        return ROFS_ERR_PARAM;
    }

    rofs__lock(fs);
    fs_reset_runtime(fs);

    /* 1. 选择 generation 更大的有效 Superblock */
    err_a = sb_read(fs, 0u, &sb_a);
    err_b = sb_read(fs, 1u, &sb_b);

    if (err_a == ROFS_OK && err_b == ROFS_OK) {
        sb = (sb_a.generation >= sb_b.generation) ? sb_a : sb_b;
    } else if (err_a == ROFS_OK) {
        sb = sb_a;
    } else if (err_b == ROFS_OK) {
        sb = sb_b;
    } else {
        rofs__unlock(fs);
        return (err_a == ROFS_ERR_VERSION || err_b == ROFS_ERR_VERSION)
                   ? ROFS_ERR_VERSION : ROFS_ERR_FORMAT;
    }
    fs->generation = sb.generation;

    memset(&m, 0, sizeof(m));

    /* 2. 逐块扫描，恢复日志状态并挑选活动块 */
    for (block = 0; block < fs->block_count; block++) {
        err = rofs__blk_read_hdr(fs, block, &bh);
        if (err != ROFS_OK) {
            fs->free_blocks++;           /* 无有效 Header：空闲块 */
            continue;
        }

        err = rofs__scan_block(fs, block, mount_cb, &m, &last);
        if (err != ROFS_OK) {
            rofs__unlock(fs);
            return err;
        }

        /* 有剩余空间且块 sequence 更大的块优先作为活动块 */
        if (last + ROFS_REC_HDR_SIZE <= fs->block_size &&
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
    if (best_block != ROFS_BLOCK_NONE) {
        fs->active_block = best_block;
    } else {
        fs->active_block = ROFS_BLOCK_NONE;
        fs->active_off = 0u;
    }

    /* 3. 根目录必须存在且是目录 */
    err = rofs__inode_find(fs, ROFS_ROOT_INODE, &root);
    if (err != ROFS_OK) {
        rofs__unlock(fs);
        return (err == ROFS_ERR_NOENT) ? ROFS_ERR_FORMAT : err;
    }
    if (root.type != ROFS_TYPE_DIR) {
        rofs__unlock(fs);
        return ROFS_ERR_CORRUPT;
    }

    fs->mounted = 1u;
    rofs__unlock(fs);
    return ROFS_OK;
}

/* ============================================================================
 * Unmount / Sync
 * ==========================================================================*/

/*
 * 卸载：同步介质、清空句柄、标记未挂载。
 * 不检查是否有文件仍打开（句柄会直接失效）。
 */
int rofs_unmount(rofs_t *fs)
{
    uint32_t i;

    if (fs == NULL) {
        return ROFS_ERR_PARAM;
    }

    rofs__lock(fs);
    if (fs->mounted) {
        (void)rofs__sync(fs);
    }
    for (i = 0; i < ROFS_MAX_OPEN_FILES; i++) {
        fs->files[i].used = 0u;
    }
    for (i = 0; i < ROFS_MAX_OPEN_DIRS; i++) {
        fs->dirs[i].used = 0u;
    }
    fs->mounted = 0u;
    rofs__unlock(fs);
    return ROFS_OK;
}

/* 同步底层介质；未挂载返回 ROFS_ERR_STATE */
int rofs_sync(rofs_t *fs)
{
    int err;

    if (fs == NULL || !fs->mounted) {
        return ROFS_ERR_STATE;
    }
    rofs__lock(fs);
    err = rofs__sync(fs);
    rofs__unlock(fs);
    return err;
}

/* ============================================================================
 * 统计
 * ==========================================================================*/

/*
 * 空间统计。
 * free_bytes = 完全空闲块 + 活动块尾部剩余；
 * 已分配但只用了部分空间的旧块，其余部分不计入 free（V1 无 GC）。
 */
int rofs_statfs(rofs_t *fs, rofs_statfs_t *stat)
{
    uint32_t total;
    uint32_t free_bytes;

    if (fs == NULL || stat == NULL || !fs->mounted) {
        return ROFS_ERR_STATE;
    }

    rofs__lock(fs);
    total = fs->block_count * fs->block_size;
    free_bytes = fs->free_blocks * fs->block_size;
    if (fs->active_block != ROFS_BLOCK_NONE &&
        fs->active_off < fs->block_size) {
        free_bytes += fs->block_size - fs->active_off;
    }

    stat->total_bytes = total;
    stat->free_bytes = free_bytes;
    stat->used_bytes = total - free_bytes;
    stat->total_blocks = fs->block_count;
    stat->free_blocks = fs->free_blocks;
    stat->block_size = fs->block_size;
    rofs__unlock(fs);
    return ROFS_OK;
}

/* 剩余空间快捷接口 */
int rofs_free_space(rofs_t *fs, uint32_t *size)
{
    rofs_statfs_t stat;
    int err;

    if (size == NULL) {
        return ROFS_ERR_PARAM;
    }
    err = rofs_statfs(fs, &stat);
    if (err != ROFS_OK) {
        return err;
    }
    *size = stat.free_bytes;
    return ROFS_OK;
}

/* 总空间快捷接口 */
int rofs_total_space(rofs_t *fs, uint32_t *size)
{
    rofs_statfs_t stat;
    int err;

    if (size == NULL) {
        return ROFS_ERR_PARAM;
    }
    err = rofs_statfs(fs, &stat);
    if (err != ROFS_OK) {
        return err;
    }
    *size = stat.total_bytes;
    return ROFS_OK;
}

/*
 * 基础自检：
 *   - Superblock A 或 B 至少一个有效；
 *   - 扫描所有块中的有效记录（CRC 由扫描过程校验）。
 * 注意：块尾未提交记录属于正常掉电残留，不算错误。
 */
int rofs_check(rofs_t *fs)
{
    rofs_sb_t sb;
    int err;

    if (fs == NULL || !fs->mounted) {
        return ROFS_ERR_STATE;
    }

    rofs__lock(fs);
    err = sb_read(fs, 0u, &sb);
    if (err != ROFS_OK) {
        err = sb_read(fs, 1u, &sb);
    }
    if (err == ROFS_OK) {
        err = rofs__scan_all(fs, NULL, NULL);
    }
    rofs__unlock(fs);
    return err;
}

/*
 * 统计有效块头的擦除次数。
 * 空闲块（Header 无效）没有计数，不参与统计。
 */
int rofs_erase_stat(rofs_t *fs, uint32_t *min_count,
                    uint32_t *max_count, uint32_t *avg_count)
{
    rofs_blk_hdr_t bh;
    uint32_t block;
    uint32_t count = 0u;
    uint32_t sum = 0u;
    uint32_t min_v = 0xFFFFFFFFu;
    uint32_t max_v = 0u;

    if (fs == NULL || min_count == NULL || max_count == NULL ||
        avg_count == NULL) {
        return ROFS_ERR_PARAM;
    }

    rofs__lock(fs);
    for (block = 0; block < fs->block_count; block++) {
        if (rofs__blk_read_hdr(fs, block, &bh) != ROFS_OK) {
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
    rofs__unlock(fs);

    if (count == 0u) {
        return ROFS_ERR_NOENT;
    }
    *min_count = min_v;
    *max_count = max_v;
    *avg_count = sum / count;
    return ROFS_OK;
}

/* ============================================================================
 * 事务 / GC / Checkpoint（V1 占位）
 * ==========================================================================*/

/* 多记录事务尚未实现 */
int rofs_tx_begin(rofs_t *fs)
{
    (void)fs;
    return ROFS_ERR_NOTSUP;
}

int rofs_tx_commit(rofs_t *fs)
{
    (void)fs;
    return ROFS_ERR_NOTSUP;
}

int rofs_tx_abort(rofs_t *fs)
{
    (void)fs;
    return ROFS_ERR_NOTSUP;
}

/* GC 尚未实现 */
int rofs_gc(rofs_t *fs)
{
    (void)fs;
    return ROFS_ERR_NOTSUP;
}

/* 根据 gc_threshold 判断是否需要 GC；阈值为 0 表示不做判断 */
int rofs_gc_needed(rofs_t *fs)
{
    rofs_statfs_t stat;
    int err;

    if (fs == NULL) {
        return ROFS_ERR_PARAM;
    }
    err = rofs_statfs(fs, &stat);
    if (err != ROFS_OK) {
        return err;
    }
    if (fs->cfg.gc_threshold == 0u) {
        return 0;
    }
    return (stat.free_bytes < fs->cfg.gc_threshold) ? 1 : 0;
}

/* Checkpoint 尚未实现 */
int rofs_checkpoint(rofs_t *fs)
{
    (void)fs;
    return ROFS_ERR_NOTSUP;
}

/* 错误码到可读字符串 */
const char *rofs_strerror(int error)
{
    switch (error) {
    case ROFS_OK:               return "ok";
    case ROFS_ERR_PARAM:        return "invalid parameter";
    case ROFS_ERR_IO:           return "io error";
    case ROFS_ERR_CRC:          return "crc error";
    case ROFS_ERR_FORMAT:       return "not formatted";
    case ROFS_ERR_VERSION:      return "version mismatch";
    case ROFS_ERR_NOENT:        return "no such entry";
    case ROFS_ERR_EXIST:        return "already exists";
    case ROFS_ERR_NOTDIR:       return "not a directory";
    case ROFS_ERR_ISDIR:        return "is a directory";
    case ROFS_ERR_NOSPC:        return "no space left";
    case ROFS_ERR_NOMEM:        return "no memory";
    case ROFS_ERR_BUSY:         return "busy";
    case ROFS_ERR_INVAL:        return "invalid argument";
    case ROFS_ERR_READONLY:     return "read only";
    case ROFS_ERR_CORRUPT:      return "corrupted";
    case ROFS_ERR_NOTEMPTY:     return "not empty";
    case ROFS_ERR_BADF:         return "bad file handle";
    case ROFS_ERR_NOTSUP:       return "not supported";
    case ROFS_ERR_TIMEOUT:      return "timeout";
    case ROFS_ERR_STATE:        return "invalid state";
    case ROFS_ERR_POWERLOSS:    return "power loss";
    case ROFS_ERR_BADBLOCK:     return "bad block";
    default:                    return "unknown error";
    }
}
