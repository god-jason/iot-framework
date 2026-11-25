/*
 * fkv_log.c - 记录日志层：CRC32、记录读写（Commit 最后写）、日志扫描
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

#include "fkv_internal.h"

/* 记录 CRC：Header(crc 字段按 0) + Payload */
static uint32_t rec_crc(const fkv_rec_hdr_t *hdr, const void *payload)
{
    fkv_rec_hdr_t h = *hdr;
    uint32_t crc;

    h.crc32 = 0U;
    crc = crc32_update(0xFFFFFFFFU, &h, (uint32_t)sizeof(h));
    if (payload != NULL) {
        crc = crc32_update(crc, payload, h.length);
    }
    return crc ^ 0xFFFFFFFFU;
}

/* ============================== 块设备封装 ============================== */

int fkv_dev_read(fkv_t *fs, uint32_t addr, void *buf, uint32_t size)
{
    if (size == 0U) {
        return FKV_OK;
    }
    return (fs->dev.read(fs->dev.ctx, addr, buf, size) == 0) ? FKV_OK
                                                             : FKV_ERR_IO;
}

int fkv_dev_program(fkv_t *fs, uint32_t addr, const void *buf, uint32_t size)
{
    if (size == 0U) {
        return FKV_OK;
    }
    return (fs->dev.program(fs->dev.ctx, addr, buf, size) == 0) ? FKV_OK
                                                                : FKV_ERR_IO;
}

int fkv_dev_erase(fkv_t *fs, uint32_t addr, uint32_t size)
{
    return (fs->dev.erase(fs->dev.ctx, addr, size) == 0) ? FKV_OK
                                                         : FKV_ERR_IO;
}

/* ============================== 记录头 ============================== */

int fkv_hdr_sane(const fkv_t *fs, const fkv_rec_hdr_t *hdr)
{
    if (hdr->magic != FKV_REC_MAGIC || hdr->version != FKV_VERSION) {
        return 0;
    }
    if (hdr->sequence == 0U) {
        return 0;
    }
    if ((hdr->flags & (uint8_t)~FKV_REC_FLAG_DELETED) != 0U) {
        return 0;
    }
    if (hdr->length > fs->max_value) {
        return 0;
    }
    if ((hdr->flags & FKV_REC_FLAG_DELETED) != 0U && hdr->length != 0U) {
        return 0;                       /* 墓碑记录不带数据 */
    }
    return 1;
}

int fkv_hdr_read(fkv_t *fs, uint32_t addr, fkv_rec_hdr_t *hdr)
{
    int rc;

    if (hdr == NULL) {
        return FKV_ERR_PARAM;
    }
    rc = fkv_dev_read(fs, addr, hdr, FKV_REC_HDR_SIZE);
    if (rc != FKV_OK) {
        return rc;
    }
    return fkv_hdr_sane(fs, hdr) ? FKV_OK : FKV_ERR_CORRUPT;
}

int fkv_rec_committed(fkv_t *fs, uint32_t rec_addr,
                       const fkv_rec_hdr_t *hdr)
{
    uint32_t commit = 0U;
    uint32_t addr = rec_addr + fkv_commit_off(fs, hdr->length);
    int rc;

    rc = fkv_dev_read(fs, addr, &commit, FKV_COMMIT_SIZE);
    if (rc != FKV_OK) {
        return rc;
    }
    return (commit == FKV_COMMIT_MAGIC) ? FKV_OK : FKV_ERR_CORRUPT;
}

/* 回读 Payload 并校验 CRC（Header 的 crc 字段按 0 参与） */
static int rec_payload_crc_ok(fkv_t *fs, uint32_t rec_addr,
                              const fkv_rec_hdr_t *hdr)
{
    fkv_rec_hdr_t h = *hdr;
    uint8_t chunk[32];
    uint32_t crc;
    uint32_t left = (uint32_t)hdr->length;
    uint32_t off = 0U;
    int rc;

    h.crc32 = 0U;
    crc = crc32_update(0xFFFFFFFFU, &h, (uint32_t)sizeof(h));
    while (left > 0U) {
        uint32_t n = (left > sizeof(chunk)) ? (uint32_t)sizeof(chunk) : left;

        rc = fkv_dev_read(fs, rec_addr + FKV_REC_HDR_SIZE + off, chunk, n);
        if (rc != FKV_OK) {
            return rc;
        }
        crc = crc32_update(crc, chunk, n);
        off += n;
        left -= n;
    }
    return ((crc ^ 0xFFFFFFFFU) == hdr->crc32) ? FKV_OK : FKV_ERR_CRC;
}

int fkv_rec_valid(fkv_t *fs, uint32_t rec_addr, const fkv_rec_hdr_t *hdr)
{
    int rc;

    rc = fkv_rec_committed(fs, rec_addr, hdr);
    if (rc != FKV_OK) {
        return rc;
    }
    return rec_payload_crc_ok(fs, rec_addr, hdr);
}

int fkv_rec_read(fkv_t *fs, uint32_t rec_addr, const fkv_rec_hdr_t *hdr,
                  uint32_t off, void *buf, uint32_t len)
{
    if (off > (uint32_t)hdr->length || len > (uint32_t)hdr->length - off) {
        return FKV_ERR_PARAM;
    }
    return fkv_dev_read(fs, rec_addr + FKV_REC_HDR_SIZE + off, buf, len);
}

/* ============================== 记录写入 ============================== */

int fkv_rec_write(fkv_t *fs, uint32_t base, uint32_t *off, uint32_t key,
                   uint8_t flags, const void *payload, uint32_t length,
                   uint32_t seq)
{
    fkv_rec_hdr_t hdr;
    fkv_rec_hdr_t check;
    uint32_t total = fkv_rec_size(fs, length);
    uint32_t commit_off = fkv_commit_off(fs, length);
    uint32_t addr;
    uint32_t commit = FKV_COMMIT_MAGIC;
    int rc;

    if (off == NULL || length > fs->max_value || length > 0xFFFFU) {
        return FKV_ERR_PARAM;
    }
    if (*off + total > fs->region_size) {
        return FKV_ERR_NOSPC;
    }

    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = FKV_REC_MAGIC;
    hdr.version = FKV_VERSION;
    hdr.length = (uint16_t)length;
    hdr.sequence = seq;
    hdr.key = key;
    hdr.flags = flags;
    hdr.crc32 = rec_crc(&hdr, payload);

    addr = base + *off;

    /* 1) Header + Payload */
    rc = fkv_dev_program(fs, addr, &hdr, FKV_REC_HDR_SIZE);
    if (rc == FKV_OK && length > 0U) {
        rc = fkv_dev_program(fs, addr + FKV_REC_HDR_SIZE, payload, length);
    }
    if (rc != FKV_OK) {
        return rc;
    }

    /* 2) 回读校验 Header 与 Payload CRC */
    rc = fkv_dev_read(fs, addr, &check, FKV_REC_HDR_SIZE);
    if (rc != FKV_OK) {
        return rc;
    }
    if (memcmp(&check, &hdr, FKV_REC_HDR_SIZE) != 0) {
        return FKV_ERR_IO;
    }
    rc = rec_payload_crc_ok(fs, addr, &check);
    if (rc != FKV_OK) {
        return FKV_ERR_IO;              /* 编程校验失败，不写 Commit */
    }

    /* 3) 最后写 Commit */
    rc = fkv_dev_program(fs, addr + commit_off, &commit, FKV_COMMIT_SIZE);
    if (rc != FKV_OK) {
        return rc;
    }

    *off += total;
    return FKV_OK;
}

/* ============================== 日志扫描 ============================== */

int fkv_scan(fkv_t *fs, fkv_scan_cb_t cb, void *ctx)
{
    uint32_t base = fkv_region_addr(fs, fs->active);
    uint32_t off = FKV_SUPER_SIZE;

    while (off + FKV_REC_HDR_SIZE + FKV_COMMIT_SIZE <= fs->write_off) {
        fkv_rec_hdr_t hdr;
        uint32_t total;
        int rc;

        rc = fkv_dev_read(fs, base + off, &hdr, FKV_REC_HDR_SIZE);
        if (rc != FKV_OK) {
            return rc;
        }
        if (hdr.magic == FKV_ERASED_U32) {
            break;                      /* 空尾 */
        }
        if (!fkv_hdr_sane(fs, &hdr)) {
            break;                      /* 损坏：日志到此为止 */
        }
        total = fkv_rec_size(fs, hdr.length);
        if (off + total > fs->region_size) {
            break;
        }
        rc = fkv_rec_committed(fs, base + off, &hdr);
        if (rc != FKV_OK) {
            break;                      /* 未提交的半成品 */
        }
        if (cb != NULL) {
            rc = cb(fs, &hdr, base + off, ctx);
            if (rc != FKV_OK) {
                return rc;
            }
        }
        off += total;
    }
    return FKV_OK;
}

/* ============================== 追加与空间 ============================== */

int fkv_append(fkv_t *fs, uint32_t key, uint8_t flags,
                const void *payload, uint32_t length)
{
    uint32_t need = fkv_rec_size(fs, length);
    uint32_t off;
    int rc;

    if (!fs->mounted) {
        return FKV_ERR_STATE;
    }
    if (fs->readonly) {
        return FKV_ERR_READONLY;
    }

    rc = fkv_ensure_space(fs, need);
    if (rc != FKV_OK) {
        return rc;
    }

    off = fs->write_off;
    rc = fkv_rec_write(fs, fkv_region_addr(fs, fs->active), &off, key,
                        flags, payload, length, fs->seq);
    if (rc != FKV_OK) {
        return rc;
    }

    fs->write_off = off;
    fs->seq++;
    fs->record_count++;
    return FKV_OK;
}

int fkv_ensure_space(fkv_t *fs, uint32_t need)
{
    int rc;

    while (fs->region_size - fs->write_off < need) {
        rc = fkv_compact(fs);
        if (rc != FKV_OK) {
            return rc;
        }
        if (fs->region_size - fs->write_off < need) {
            return FKV_ERR_NOSPC;       /* 有效数据本身就放不下 */
        }
    }
    return FKV_OK;
}
