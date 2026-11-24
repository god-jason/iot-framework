/*
 * rofs.c - 挂载、镜像信息与完整性校验
 * ============================================================================
 *
 * Super Header（32B，小端）：
 *   +0  magic         4B   "ROFS"
 *   +4  version       2B   格式版本
 *   +6  header_size   2B   固定 32
 *   +8  image_size    4B   镜像总大小
 *   +12 record_count  4B   记录数量
 *   +16 records_offset 4B  第一条记录偏移
 *   +20 records_size  4B   记录区大小
 *   +24 image_crc32   4B   整镜像 CRC32（计算时本字段视为 0）
 *   +28 flags         4B   镜像标志
 */

#include "rofs_internal.h"

/* 介质读取封装：offset 为镜像内偏移，自动加上挂载基址 */
int rofs__read(rofs_t *fs, uint32_t offset, void *buf, uint32_t size)
{
    if (fs == NULL || fs->read == NULL) {
        return ROFS_ERR_PARAM;
    }
    if ((buf == NULL && size != 0u)) {
        return ROFS_ERR_PARAM;
    }
    if (size == 0u) {
        return ROFS_OK;
    }
    if (offset > fs->image_size || size > fs->image_size - offset) {
        return ROFS_ERR_RANGE;
    }
    if (fs->read(fs->ctx, fs->base_offset + offset, buf, size) != 0) {
        return ROFS_ERR_IO;
    }
    return ROFS_OK;
}

int rofs_init(rofs_t *fs)
{
    if (fs == NULL) {
        return ROFS_ERR_PARAM;
    }
    memset(fs, 0, sizeof(*fs));
    return ROFS_OK;
}

int rofs_mount(rofs_t *fs, uint32_t base_offset,
               rofs_read_fn read, void *ctx)
{
    uint8_t buf[ROFS_SUPER_SIZE];
    uint8_t end[4];
    uint32_t magic;
    uint32_t image_size;
    uint32_t records_offset;
    uint32_t records_size;
    uint32_t record_count;
    int err;

    if (fs == NULL || read == NULL) {
        return ROFS_ERR_PARAM;
    }

    memset(fs, 0, sizeof(*fs));
    fs->base_offset = base_offset;
    fs->read = read;
    fs->ctx = ctx;

    /* 先按固定长度读取 Super Header */
    fs->image_size = ROFS_SUPER_SIZE;
    err = rofs__read(fs, 0u, buf, sizeof(buf));
    if (err != ROFS_OK) {
        return err;
    }

    magic = rofs__get_u32(&buf[ROFS_SB_OFF_MAGIC]);
    if (magic != ROFS_MAGIC) {
        return ROFS_ERR_MAGIC;
    }
    fs->version = rofs__get_u16(&buf[ROFS_SB_OFF_VERSION]);
    if (fs->version != ROFS_VERSION) {
        return ROFS_ERR_VERSION;
    }
    if (rofs__get_u16(&buf[ROFS_SB_OFF_HDR_SIZE]) != ROFS_SUPER_SIZE) {
        return ROFS_ERR_FORMAT;
    }

    image_size = rofs__get_u32(&buf[ROFS_SB_OFF_IMAGE_SIZE]);
    record_count = rofs__get_u32(&buf[ROFS_SB_OFF_REC_COUNT]);
    records_offset = rofs__get_u32(&buf[ROFS_SB_OFF_REC_OFFSET]);
    records_size = rofs__get_u32(&buf[ROFS_SB_OFF_REC_SIZE]);

    if (image_size < ROFS_SUPER_SIZE + 4u) {
        return ROFS_ERR_FORMAT;
    }
    if (records_offset < ROFS_SUPER_SIZE || records_offset > image_size) {
        return ROFS_ERR_FORMAT;
    }
    if (records_size > image_size - records_offset ||
        image_size - records_offset - records_size != 4u) {
        return ROFS_ERR_FORMAT;
    }

    fs->image_size = image_size;
    fs->records_offset = records_offset;
    fs->records_size = records_size;
    fs->record_count = record_count;
    fs->image_crc = rofs__get_u32(&buf[ROFS_SB_OFF_IMAGE_CRC]);
    fs->flags = rofs__get_u32(&buf[ROFS_SB_OFF_FLAGS]);

    /* End 标记位于记录区末尾 */
    err = rofs__read(fs, rofs__records_end(fs), end, sizeof(end));
    if (err != ROFS_OK) {
        return err;
    }
    if (rofs__get_u32(end) != ROFS_END_MAGIC) {
        return ROFS_ERR_FORMAT;
    }

    fs->mounted = 1u;
    return ROFS_OK;
}

int rofs_unmount(rofs_t *fs)
{
    if (fs == NULL) {
        return ROFS_ERR_PARAM;
    }
    fs->mounted = 0u;
    return ROFS_OK;
}

int rofs_get_info(rofs_t *fs, rofs_info_t *info)
{
    if (fs == NULL || info == NULL || fs->mounted == 0u) {
        return ROFS_ERR_INVALID;
    }
    info->image_size = fs->image_size;
    info->records_offset = fs->records_offset;
    info->records_size = fs->records_size;
    info->record_count = fs->record_count;
    info->flags = fs->flags;
    info->version = fs->version;
    return ROFS_OK;
}

/* 逐条校验记录边界、文件名与 Body CRC */
static int check_records(rofs_t *fs)
{
    uint32_t off = fs->records_offset;
    uint32_t end = rofs__records_end(fs);
    uint32_t i;

    for (i = 0u; i < fs->record_count; i++) {
        rofs_rec_t rec;
        uint32_t body;
        uint32_t left;
        uint32_t crc = 0xFFFFFFFFu;
        uint8_t chunk[ROFS_CRC_CHUNK];
        int err;

        if (off > end || end - off < ROFS_RECORD_SIZE) {
            return ROFS_ERR_FORMAT;
        }
        err = rofs__rec_read(fs, off, &rec);
        if (err != ROFS_OK) {
            return err;
        }
        if (rec.name_len == 0u) {
            return ROFS_ERR_FORMAT;
        }
        body = off + ROFS_RECORD_SIZE + rec.name_len;
        if (body > end || rec.size > end - body) {
            return ROFS_ERR_FORMAT;
        }

        left = rec.size;
        while (left > 0u) {
            uint32_t n = (left > ROFS_CRC_CHUNK) ? ROFS_CRC_CHUNK : left;

            err = rofs__read(fs, body, chunk, n);
            if (err != ROFS_OK) {
                return err;
            }
            crc = rofs__crc32_update(crc, chunk, n);
            body += n;
            left -= n;
        }
        if ((crc ^ 0xFFFFFFFFu) != rec.crc) {
            return ROFS_ERR_CRC;
        }
        off = off + ROFS_RECORD_SIZE + rec.name_len + rec.size;
    }

    return (off == end) ? ROFS_OK : ROFS_ERR_FORMAT;
}

/* 校验整个镜像的 CRC，计算时把 image_crc32 字段视为 0 */
static int check_image_crc(rofs_t *fs)
{
    uint8_t chunk[ROFS_CRC_CHUNK];
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t pos = 0u;
    int err;

    while (pos < fs->image_size) {
        uint32_t n = fs->image_size - pos;
        uint32_t i;

        if (n > ROFS_CRC_CHUNK) {
            n = ROFS_CRC_CHUNK;
        }
        err = rofs__read(fs, pos, chunk, n);
        if (err != ROFS_OK) {
            return err;
        }
        for (i = 0u; i < n; i++) {
            uint32_t off = pos + i;

            if (off >= ROFS_SB_OFF_IMAGE_CRC &&
                off < ROFS_SB_OFF_IMAGE_CRC + 4u) {
                chunk[i] = 0u;
            }
        }
        crc = rofs__crc32_update(crc, chunk, n);
        pos += n;
    }

    crc ^= 0xFFFFFFFFu;
    return (crc == fs->image_crc) ? ROFS_OK : ROFS_ERR_CRC;
}

int rofs_check(rofs_t *fs)
{
    int err;

    if (fs == NULL || fs->mounted == 0u) {
        return ROFS_ERR_INVALID;
    }

    err = check_records(fs);
    if (err != ROFS_OK) {
        return err;
    }
    return check_image_crc(fs);
}
