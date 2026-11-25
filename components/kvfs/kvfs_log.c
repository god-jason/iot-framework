/*
 * kvfs_log.c - 记录日志层：CRC32、记录读写（Commit 最后写）、日志扫描
 * ============================================================================
 *
 * 与 config.c 相同的安全约定：
 *   1) 先写记录头与 Payload；
 *   2) 回读 Header 并重新计算 Payload CRC，确认写入正确；
 *   3) 最后写 4 字节 Commit 标记 —— 只有 Commit 存在，记录才被认可。
 *
 * 分区 SUPER 也是“最后提交”：压缩时先复制完全部有效记录，再写 SUPER。
 * 掉电发生在 SUPER 之前时，该分区不会被挂载选中。
 */

#include "kvfs_internal.h"

/* 记录 CRC：Header(crc 字段按 0) + Payload */
static uint32_t rec_crc(const kvfs_rec_hdr_t *hdr, const void *payload)
{
    kvfs_rec_hdr_t h = *hdr;
    uint32_t crc;

    h.crc32 = 0U;
    crc = crc32_update(0xFFFFFFFFU, &h, (uint32_t)sizeof(h));
    if (payload != NULL) {
        crc = crc32_update(crc, payload, h.length);
    }
    return crc ^ 0xFFFFFFFFU;
}

/* ============================== 块设备封装 ============================== */

int kvfs_dev_read(kvfs_t *fs, uint32_t addr, void *buf, uint32_t size)
{
    if (size == 0U) {
        return KVFS_OK;
    }
    return (fs->dev.read(fs->dev.ctx, addr, buf, size) == 0) ? KVFS_OK
                                                             : KVFS_ERR_IO;
}

int kvfs_dev_program(kvfs_t *fs, uint32_t addr, const void *buf, uint32_t size)
{
    if (size == 0U) {
        return KVFS_OK;
    }
    return (fs->dev.program(fs->dev.ctx, addr, buf, size) == 0) ? KVFS_OK
                                                                : KVFS_ERR_IO;
}

int kvfs_dev_erase(kvfs_t *fs, uint32_t addr, uint32_t size)
{
    return (fs->dev.erase(fs->dev.ctx, addr, size) == 0) ? KVFS_OK
                                                         : KVFS_ERR_IO;
}

/* ============================== 记录头 ============================== */

int kvfs_hdr_sane(const kvfs_t *fs, const kvfs_rec_hdr_t *hdr)
{
    if (hdr->magic != KVFS_REC_MAGIC || hdr->version != KVFS_VERSION) {
        return 0;
    }
    if (hdr->sequence == 0U) {
        return 0;
    }
    if ((hdr->flags & (uint8_t)~KVFS_REC_FLAG_DELETED) != 0U) {
        return 0;
    }
    if (hdr->length > fs->max_value) {
        return 0;
    }
    if ((hdr->flags & KVFS_REC_FLAG_DELETED) != 0U && hdr->length != 0U) {
        return 0;                       /* 墓碑记录不带数据 */
    }
    return 1;
}

int kvfs_hdr_read(kvfs_t *fs, uint32_t addr, kvfs_rec_hdr_t *hdr)
{
    int rc;

    if (hdr == NULL) {
        return KVFS_ERR_PARAM;
    }
    rc = kvfs_dev_read(fs, addr, hdr, KVFS_REC_HDR_SIZE);
    if (rc != KVFS_OK) {
        return rc;
    }
    return kvfs_hdr_sane(fs, hdr) ? KVFS_OK : KVFS_ERR_CORRUPT;
}

int kvfs_rec_committed(kvfs_t *fs, uint32_t rec_addr,
                       const kvfs_rec_hdr_t *hdr)
{
    uint32_t commit = 0U;
    uint32_t addr = rec_addr + kvfs_commit_off(fs, hdr->length);
    int rc;

    rc = kvfs_dev_read(fs, addr, &commit, KVFS_COMMIT_SIZE);
    if (rc != KVFS_OK) {
        return rc;
    }
    return (commit == KVFS_COMMIT_MAGIC) ? KVFS_OK : KVFS_ERR_CORRUPT;
}

/* 回读 Payload 并校验 CRC（Header 的 crc 字段按 0 参与） */
static int rec_payload_crc_ok(kvfs_t *fs, uint32_t rec_addr,
                              const kvfs_rec_hdr_t *hdr)
{
    kvfs_rec_hdr_t h = *hdr;
    uint8_t chunk[32];
    uint32_t crc;
    uint32_t left = (uint32_t)hdr->length;
    uint32_t off = 0U;
    int rc;

    h.crc32 = 0U;
    crc = crc32_update(0xFFFFFFFFU, &h, (uint32_t)sizeof(h));
    while (left > 0U) {
        uint32_t n = (left > sizeof(chunk)) ? (uint32_t)sizeof(chunk) : left;

        rc = kvfs_dev_read(fs, rec_addr + KVFS_REC_HDR_SIZE + off, chunk, n);
        if (rc != KVFS_OK) {
            return rc;
        }
        crc = crc32_update(crc, chunk, n);
        off += n;
        left -= n;
    }
    return ((crc ^ 0xFFFFFFFFU) == hdr->crc32) ? KVFS_OK : KVFS_ERR_CRC;
}

int kvfs_rec_valid(kvfs_t *fs, uint32_t rec_addr, const kvfs_rec_hdr_t *hdr)
{
    int rc;

    rc = kvfs_rec_committed(fs, rec_addr, hdr);
    if (rc != KVFS_OK) {
        return rc;
    }
    return rec_payload_crc_ok(fs, rec_addr, hdr);
}

int kvfs_rec_read(kvfs_t *fs, uint32_t rec_addr, const kvfs_rec_hdr_t *hdr,
                  uint32_t off, void *buf, uint32_t len)
{
    if (off > (uint32_t)hdr->length || len > (uint32_t)hdr->length - off) {
        return KVFS_ERR_PARAM;
    }
    return kvfs_dev_read(fs, rec_addr + KVFS_REC_HDR_SIZE + off, buf, len);
}

/* ============================== 记录写入 ============================== */

int kvfs_rec_write(kvfs_t *fs, uint32_t base, uint32_t *off, uint32_t key,
                   uint8_t flags, const void *payload, uint32_t length,
                   uint32_t seq)
{
    kvfs_rec_hdr_t hdr;
    kvfs_rec_hdr_t check;
    uint32_t total = kvfs_rec_size(fs, length);
    uint32_t commit_off = kvfs_commit_off(fs, length);
    uint32_t addr;
    uint32_t commit = KVFS_COMMIT_MAGIC;
    int rc;

    if (off == NULL || length > fs->max_value || length > 0xFFFFU) {
        return KVFS_ERR_PARAM;
    }
    if (*off + total > fs->region_size) {
        return KVFS_ERR_NOSPC;
    }

    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = KVFS_REC_MAGIC;
    hdr.version = KVFS_VERSION;
    hdr.length = (uint16_t)length;
    hdr.sequence = seq;
    hdr.key = key;
    hdr.flags = flags;
    hdr.crc32 = rec_crc(&hdr, payload);

    addr = base + *off;

    /* 1) Header + Payload */
    rc = kvfs_dev_program(fs, addr, &hdr, KVFS_REC_HDR_SIZE);
    if (rc == KVFS_OK && length > 0U) {
        rc = kvfs_dev_program(fs, addr + KVFS_REC_HDR_SIZE, payload, length);
    }
    if (rc != KVFS_OK) {
        return rc;
    }

    /* 2) 回读校验 Header 与 Payload CRC */
    rc = kvfs_dev_read(fs, addr, &check, KVFS_REC_HDR_SIZE);
    if (rc != KVFS_OK) {
        return rc;
    }
    if (memcmp(&check, &hdr, KVFS_REC_HDR_SIZE) != 0) {
        return KVFS_ERR_IO;
    }
    rc = rec_payload_crc_ok(fs, addr, &check);
    if (rc != KVFS_OK) {
        return KVFS_ERR_IO;              /* 编程校验失败，不写 Commit */
    }

    /* 3) 最后写 Commit */
    rc = kvfs_dev_program(fs, addr + commit_off, &commit, KVFS_COMMIT_SIZE);
    if (rc != KVFS_OK) {
        return rc;
    }

    *off += total;
    return KVFS_OK;
}

/* ============================== 日志扫描 ============================== */

int kvfs_scan(kvfs_t *fs, kvfs_scan_cb_t cb, void *ctx)
{
    uint32_t base = kvfs_region_addr(fs, fs->active);
    uint32_t off = KVFS_SUPER_SIZE;

    while (off + KVFS_REC_HDR_SIZE + KVFS_COMMIT_SIZE <= fs->write_off) {
        kvfs_rec_hdr_t hdr;
        uint32_t total;
        int rc;

        rc = kvfs_dev_read(fs, base + off, &hdr, KVFS_REC_HDR_SIZE);
        if (rc != KVFS_OK) {
            return rc;
        }
        if (hdr.magic == KVFS_ERASED_U32) {
            break;                      /* 空尾 */
        }
        if (!kvfs_hdr_sane(fs, &hdr)) {
            break;                      /* 损坏：日志到此为止 */
        }
        total = kvfs_rec_size(fs, hdr.length);
        if (off + total > fs->region_size) {
            break;
        }
        rc = kvfs_rec_committed(fs, base + off, &hdr);
        if (rc != KVFS_OK) {
            break;                      /* 未提交的半成品 */
        }
        if (cb != NULL) {
            rc = cb(fs, &hdr, base + off, ctx);
            if (rc != KVFS_OK) {
                return rc;
            }
        }
        off += total;
    }
    return KVFS_OK;
}

/* ============================== 追加与空间 ============================== */

int kvfs_append(kvfs_t *fs, uint32_t key, uint8_t flags,
                const void *payload, uint32_t length)
{
    uint32_t need = kvfs_rec_size(fs, length);
    uint32_t off;
    int rc;

    if (!fs->mounted) {
        return KVFS_ERR_STATE;
    }
    if (fs->readonly) {
        return KVFS_ERR_READONLY;
    }

    rc = kvfs_ensure_space(fs, need);
    if (rc != KVFS_OK) {
        return rc;
    }

    off = fs->write_off;
    rc = kvfs_rec_write(fs, kvfs_region_addr(fs, fs->active), &off, key,
                        flags, payload, length, fs->seq);
    if (rc != KVFS_OK) {
        return rc;
    }

    fs->write_off = off;
    fs->seq++;
    fs->record_count++;
    return KVFS_OK;
}

int kvfs_ensure_space(kvfs_t *fs, uint32_t need)
{
    int rc;

    while (fs->region_size - fs->write_off < need) {
        rc = kvfs_compact(fs);
        if (rc != KVFS_OK) {
            return rc;
        }
        if (fs->region_size - fs->write_off < need) {
            return KVFS_ERR_NOSPC;       /* 有效数据本身就放不下 */
        }
    }
    return KVFS_OK;
}
