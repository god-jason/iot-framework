/*
 * kvfs.c - 实例生命周期：初始化、格式化、挂载、统计、校验
 * ============================================================================
 *
 * 挂载流程（对应 config.c 的“扫描两区取最新”）：
 *   1) 分别读取 A/B 区 SUPER 并校验 CRC；
 *   2) 取代次更大的已提交分区；
 *   3) 扫描该分区日志，恢复可追加偏移与最大序号。
 *
 * 只有在压缩完成、SUPER 最后写入之后，分区才被视为已提交，
 * 因此掉电不会把“拷贝到一半”的分区误认为有效数据。
 */

#include "kvfs_internal.h"

/* 编译期确认落盘结构尺寸 */
typedef char kvfs_super_size_check[(sizeof(kvfs_super_t) == KVFS_SUPER_SIZE) ? 1 : -1];
typedef char kvfs_hdr_size_check[(sizeof(kvfs_rec_hdr_t) == KVFS_REC_HDR_SIZE) ? 1 : -1];

/* ============================== 分区 SUPER ============================== */

int kvfs_super_read(kvfs_t *fs, uint32_t index, kvfs_super_t *sb)
{
    uint32_t crc;
    int rc;

    if (sb == NULL) {
        return KVFS_ERR_PARAM;
    }
    rc = kvfs_dev_read(fs, kvfs_region_addr(fs, index), sb, KVFS_SUPER_SIZE);
    if (rc != KVFS_OK) {
        return rc;
    }
    if (sb->magic != KVFS_SUPER_MAGIC) {
        return KVFS_ERR_FORMAT;
    }
    if (sb->version != KVFS_VERSION) {
        return KVFS_ERR_VERSION;
    }
    if (sb->hdr_size != KVFS_SUPER_SIZE ||
        sb->data_start != KVFS_SUPER_SIZE || sb->generation == 0U) {
        return KVFS_ERR_FORMAT;
    }
    crc = crc32(sb, KVFS_SUPER_SIZE - 4U);
    if (crc != sb->crc32) {
        return KVFS_ERR_FORMAT;
    }
    return KVFS_OK;
}

int kvfs_super_write(kvfs_t *fs, uint32_t index, const kvfs_super_t *sb)
{
    kvfs_super_t tmp = *sb;
    kvfs_super_t check;
    uint32_t addr = kvfs_region_addr(fs, index);
    int rc;

    tmp.magic = KVFS_SUPER_MAGIC;
    tmp.version = KVFS_VERSION;
    tmp.hdr_size = KVFS_SUPER_SIZE;
    tmp.data_start = KVFS_SUPER_SIZE;
    tmp.crc32 = crc32(&tmp, KVFS_SUPER_SIZE - 4U);

    rc = kvfs_dev_program(fs, addr, &tmp, KVFS_SUPER_SIZE);
    if (rc != KVFS_OK) {
        return rc;
    }
    rc = kvfs_dev_read(fs, addr, &check, KVFS_SUPER_SIZE);
    if (rc != KVFS_OK) {
        return rc;
    }
    return (memcmp(&check, &tmp, KVFS_SUPER_SIZE) == 0) ? KVFS_OK : KVFS_ERR_IO;
}

/* ============================== 分区扫描 ============================== */

int kvfs_region_scan(kvfs_t *fs, uint32_t index, uint8_t verify_crc,
                     uint32_t *write_off, uint32_t *count, uint32_t *max_seq)
{
    uint32_t base = kvfs_region_addr(fs, index);
    uint32_t off = KVFS_SUPER_SIZE;
    uint32_t n = 0U;
    uint32_t seq = 0U;
    uint8_t broken = 0U;
    int rc;

    while (off + KVFS_REC_HDR_SIZE + KVFS_COMMIT_SIZE <= fs->region_size) {
        kvfs_rec_hdr_t hdr;
        uint32_t total;

        rc = kvfs_dev_read(fs, base + off, &hdr, KVFS_REC_HDR_SIZE);
        if (rc != KVFS_OK) {
            return rc;
        }
        if (hdr.magic == KVFS_ERASED_U32) {
            break;                      /* 空尾 */
        }
        if (!kvfs_hdr_sane(fs, &hdr)) {
            broken = 1U;
            break;
        }
        total = kvfs_rec_size(fs, hdr.length);
        if (off + total > fs->region_size) {
            broken = 1U;
            break;
        }
        rc = kvfs_rec_committed(fs, base + off, &hdr);
        if (rc != KVFS_OK) {
            broken = 1U;                /* 未提交的半成品：本区不再追加 */
            break;
        }
        if (verify_crc != 0U) {
            rc = kvfs_rec_valid(fs, base + off, &hdr);
            if (rc != KVFS_OK) {
                return KVFS_ERR_CORRUPT;
            }
        }
        n++;
        if (hdr.sequence > seq) {
            seq = hdr.sequence;
        }
        off += total;
    }

    if (write_off != NULL) {
        *write_off = (broken != 0U) ? fs->region_size : off;
    }
    if (count != NULL) {
        *count = n;
    }
    if (max_seq != NULL) {
        *max_seq = seq;
    }
    return KVFS_OK;
}

/* ============================== 初始化 ============================== */

int kvfs_init(kvfs_t *fs, const kvfs_blockdev_t *dev, const kvfs_config_t *cfg)
{
    uint32_t region;
    uint32_t max_value;

    if (fs == NULL || dev == NULL || cfg == NULL ||
        dev->read == NULL || dev->program == NULL || dev->erase == NULL) {
        return KVFS_ERR_PARAM;
    }
    if (dev->erase_size == 0U || dev->total_size < 2U * dev->erase_size) {
        return KVFS_ERR_PARAM;
    }

    max_value = cfg->max_value;
    if (max_value == 0U || max_value > KVFS_VALUE_MAX) {
        max_value = KVFS_VALUE_MAX;
    }
    if (cfg->workbuf == NULL || cfg->workbuf_size < max_value) {
        return KVFS_ERR_PARAM;
    }

    memset(fs, 0, sizeof(*fs));
    fs->dev = *dev;
    fs->cfg = *cfg;
    fs->workbuf = cfg->workbuf;
    fs->workbuf_size = cfg->workbuf_size;
    fs->max_value = max_value;
    fs->readonly = (cfg->readonly != 0U) ? 1U : 0U;

    /* 两个等大分区；容量按擦除单元向下取整 */
    region = (dev->total_size / 2U / dev->erase_size) * dev->erase_size;
    if (region < KVFS_MIN_REGION_SIZE) {
        return KVFS_ERR_PARAM;
    }
    fs->region_size = region;
    fs->region_base[0] = dev->start_addr;
    fs->region_base[1] = dev->start_addr + region;

    fs->mounted = 0U;
    return KVFS_OK;
}

/* ============================== 格式化 ============================== */

int kvfs_format(kvfs_t *fs)
{
    kvfs_super_t sb;
    uint32_t i;
    int rc;

    if (fs == NULL || fs->dev.read == NULL) {
        return KVFS_ERR_PARAM;
    }
    if (fs->readonly != 0U) {
        return KVFS_ERR_READONLY;
    }

    fs->mounted = 0U;

    for (i = 0U; i < 2U; i++) {
        rc = kvfs_dev_erase(fs, fs->region_base[i], fs->region_size);
        if (rc != KVFS_OK) {
            return rc;
        }
    }

    memset(&sb, 0, sizeof(sb));
    sb.generation = 1U;
    sb.record_count = 0U;
    sb.live_bytes = 0U;
    rc = kvfs_super_write(fs, 0U, &sb);
    if (rc != KVFS_OK) {
        return rc;
    }

    fs->active = 0U;
    fs->write_off = KVFS_SUPER_SIZE;
    fs->generation = 1U;
    fs->seq = 1U;
    fs->record_count = 0U;
    fs->mounted = 1U;
    return KVFS_OK;
}

/* ============================== 挂载 ============================== */

int kvfs_mount(kvfs_t *fs)
{
    kvfs_super_t sb[2];
    int rc[2];
    uint32_t pick;
    uint32_t write_off = 0U;
    uint32_t count = 0U;
    uint32_t max_seq = 0U;
    uint32_t i;

    if (fs == NULL || fs->dev.read == NULL) {
        return KVFS_ERR_PARAM;
    }

    fs->mounted = 0U;

    for (i = 0U; i < 2U; i++) {
        rc[i] = kvfs_super_read(fs, i, &sb[i]);
    }
    if (rc[0] != KVFS_OK && rc[1] != KVFS_OK) {
        int err = KVFS_ERR_FORMAT;

        if (rc[0] == KVFS_ERR_VERSION || rc[1] == KVFS_ERR_VERSION) {
            err = KVFS_ERR_VERSION;
        }
        return err;
    }
    if (rc[0] == KVFS_OK && rc[1] == KVFS_OK) {
        pick = ((int32_t)(sb[1].generation - sb[0].generation) > 0) ? 1U : 0U;
    } else {
        pick = (rc[0] == KVFS_OK) ? 0U : 1U;
    }

    fs->active = (uint8_t)pick;
    fs->generation = sb[pick].generation;
    fs->write_off = KVFS_SUPER_SIZE;

    rc[0] = kvfs_region_scan(fs, pick, 0U, &write_off, &count, &max_seq);
    if (rc[0] != KVFS_OK) {
        return rc[0];
    }

    fs->write_off = write_off;
    fs->record_count = count;
    fs->seq = (max_seq == 0xFFFFFFFFU) ? 1U : (max_seq + 1U);
    fs->mounted = 1U;
    return KVFS_OK;
}

int kvfs_unmount(kvfs_t *fs)
{
    if (fs == NULL) {
        return KVFS_ERR_PARAM;
    }
    fs->mounted = 0U;
    return KVFS_OK;
}

int kvfs_sync(kvfs_t *fs)
{
    if (fs == NULL || !fs->mounted) {
        return KVFS_ERR_STATE;
    }
    if (fs->dev.sync == NULL) {
        return KVFS_OK;
    }
    return (fs->dev.sync(fs->dev.ctx) == 0) ? KVFS_OK : KVFS_ERR_IO;
}

/* ============================== 统计与校验 ============================== */

int kvfs_statfs(kvfs_t *fs, kvfs_statfs_t *stat)
{
    uint32_t total;
    uint32_t used;

    if (fs == NULL || stat == NULL || !fs->mounted) {
        return KVFS_ERR_STATE;
    }

    total = fs->region_size - KVFS_SUPER_SIZE;
    used = (fs->write_off > KVFS_SUPER_SIZE) ? (fs->write_off - KVFS_SUPER_SIZE)
                                            : 0U;
    if (used > total) {
        used = total;
    }

    stat->total_bytes = total;
    stat->used_bytes = used;
    stat->free_bytes = total - used;
    stat->total_blocks = 2U;
    stat->used_blocks = 1U;
    stat->block_size = fs->region_size;
    return KVFS_OK;
}

int kvfs_check(kvfs_t *fs)
{
    uint32_t write_off = 0U;
    uint32_t count = 0U;
    uint32_t max_seq = 0U;
    int rc;

    if (fs == NULL || !fs->mounted) {
        return KVFS_ERR_STATE;
    }

    rc = kvfs_region_scan(fs, fs->active, 1U, &write_off, &count, &max_seq);
    return rc;
}

int kvfs_gc(kvfs_t *fs)
{
    int rc;

    if (fs == NULL || !fs->mounted) {
        return KVFS_ERR_STATE;
    }
    if (fs->readonly != 0U) {
        return KVFS_ERR_READONLY;
    }

    rc = kvfs_compact(fs);
    return rc;
}

/* ============================== 错误信息 ============================== */

const char *kvfs_strerror(int error)
{
    switch (error) {
    case KVFS_OK:            return "ok";
    case KVFS_ERR_PARAM:     return "invalid parameter";
    case KVFS_ERR_IO:        return "io error";
    case KVFS_ERR_CRC:       return "crc error";
    case KVFS_ERR_FORMAT:    return "not formatted";
    case KVFS_ERR_VERSION:   return "version mismatch";
    case KVFS_ERR_NOENT:     return "key not found";
    case KVFS_ERR_NOSPC:     return "no space";
    case KVFS_ERR_NOMEM:     return "buffer too small";
    case KVFS_ERR_INVAL:     return "invalid value";
    case KVFS_ERR_READONLY:  return "read only";
    case KVFS_ERR_CORRUPT:   return "corrupted";
    case KVFS_ERR_STATE:     return "bad state";
    default:                return "unknown error";
    }
}
