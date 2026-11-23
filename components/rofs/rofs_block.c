/*
 * rofs_block.c - 块设备封装、Block Header 与块分配
 * ============================================================================
 *
 * 数据池由若干擦除块组成，每个块开头有一个 16B Block Header：
 *
 *   +----------------------------+
 *   | magic      (4B)            |  "!BLK"
 *   | sequence   (4B)            |  块分配顺序，越大越新
 *   | erase_count(4B)            |  擦除次数，V1 仅记录
 *   | crc        (4B)            |  前 12B 的 CRC32
 *   +----------------------------+
 *   | Record 0                   |
 *   | Record 1                   |
 *   | ...                        |
 *   +----------------------------+
 *
 * 空闲块的特征是整块为 0xFF（Block Header magic 读回 0xFFFFFFFF）。
 * 分配块时先擦除，再写入新的 Header。
 */

#include "rofs_internal.h"

/*
 * 块设备读取封装。
 * len 为 0 时直接成功，允许 buf 为 NULL（便于记录读取的可选 Payload）。
 */
int rofs__read(rofs_t *fs, uint32_t addr, void *buf, uint32_t len)
{
    if (fs == NULL || fs->dev.read == NULL) {
        return ROFS_ERR_PARAM;
    }
    if (len == 0u) {
        return ROFS_OK;
    }
    if (buf == NULL) {
        return ROFS_ERR_PARAM;
    }
    if (fs->dev.read(fs->dev.ctx, addr, buf, len) != 0) {
        return ROFS_ERR_IO;
    }
    return ROFS_OK;
}

/*
 * 块设备写入封装。
 * 与读取一样，len 为 0 时直接成功；底层非 0 统一映射为 IO 错误。
 */
int rofs__prog(rofs_t *fs, uint32_t addr, const void *buf, uint32_t len)
{
    if (fs == NULL || fs->dev.program == NULL) {
        return ROFS_ERR_PARAM;
    }
    if (len == 0u) {
        return ROFS_OK;
    }
    if (buf == NULL) {
        return ROFS_ERR_PARAM;
    }
    if (fs->dev.program(fs->dev.ctx, addr, buf, len) != 0) {
        return ROFS_ERR_IO;
    }
    return ROFS_OK;
}

/* 块设备擦除封装；addr/size 的对齐由调用者保证 */
int rofs__erase(rofs_t *fs, uint32_t addr, uint32_t len)
{
    if (fs == NULL || fs->dev.erase == NULL) {
        return ROFS_ERR_PARAM;
    }
    if (fs->dev.erase(fs->dev.ctx, addr, len) != 0) {
        return ROFS_ERR_IO;
    }
    return ROFS_OK;
}

/* 块设备同步封装；未提供 sync 回调时视为成功 */
int rofs__sync(rofs_t *fs)
{
    if (fs == NULL) {
        return ROFS_ERR_PARAM;
    }
    if (fs->dev.sync == NULL) {
        return ROFS_OK;
    }
    if (fs->dev.sync(fs->dev.ctx) != 0) {
        return ROFS_ERR_IO;
    }
    return ROFS_OK;
}

/*
 * 计算池内块的绝对地址。
 * 前两个块被 Superblock A/B 占用，因此数据块从 (block + 2) 开始。
 */
uint32_t rofs__blk_addr(rofs_t *fs, uint32_t block)
{
    return fs->dev.start_addr + (block + 2u) * fs->block_size;
}

/*
 * 读取并校验 Block Header。
 * 返回：
 *   ROFS_OK         Header 有效；
 *   ROFS_ERR_NOENT  魔数不匹配（通常表示空闲/未分配块）；
 *   ROFS_ERR_CRC    Header CRC 错误；
 *   ROFS_ERR_IO     底层读取失败。
 */
int rofs__blk_read_hdr(rofs_t *fs, uint32_t block, rofs_blk_hdr_t *hdr)
{
    uint8_t buf[ROFS_BLK_HDR_SIZE];
    uint32_t crc;
    int err;

    if (fs == NULL || hdr == NULL || block >= fs->block_count) {
        return ROFS_ERR_PARAM;
    }

    err = rofs__read(fs, rofs__blk_addr(fs, block), buf, sizeof(buf));
    if (err != ROFS_OK) {
        return err;
    }

    hdr->magic = rofs__get_u32(&buf[0]);
    hdr->sequence = rofs__get_u32(&buf[4]);
    hdr->erase_count = rofs__get_u32(&buf[8]);
    hdr->crc = rofs__get_u32(&buf[12]);

    if (hdr->magic != ROFS_BLK_MAGIC) {
        return ROFS_ERR_NOENT;
    }
    crc = rofs__crc32(buf, 12u);
    if (crc != hdr->crc) {
        return ROFS_ERR_CRC;
    }
    return ROFS_OK;
}

/*
 * 写入 Block Header 并同步。
 * 只在新分配块（已擦除）后调用一次，之后不原地修改。
 */
int rofs__blk_write_hdr(rofs_t *fs, uint32_t block, uint32_t sequence,
                        uint32_t erase_count)
{
    uint8_t buf[ROFS_BLK_HDR_SIZE];
    int err;

    if (fs == NULL || block >= fs->block_count) {
        return ROFS_ERR_PARAM;
    }

    rofs__put_u32(&buf[0], ROFS_BLK_MAGIC);
    rofs__put_u32(&buf[4], sequence);
    rofs__put_u32(&buf[8], erase_count);
    rofs__put_u32(&buf[12], rofs__crc32(buf, 12u));

    err = rofs__prog(fs, rofs__blk_addr(fs, block), buf, sizeof(buf));
    if (err != ROFS_OK) {
        return err;
    }
    return rofs__sync(fs);
}

/*
 * 分配一个空闲块。
 *
 * 查找策略：从块 0 开始，读取首 4 字节，若为 0xFFFFFFFF 则视为空闲。
 * V1 尚未实现磨损均衡，因此这里不看 erase_count，直接取第一个空闲块。
 *
 * 分配成功后：
 *   - 擦除整块；
 *   - 写入带新 sequence 的 Block Header；
 *   - 更新 active_block/active_off/free_blocks/used_bytes。
 */
int rofs__blk_alloc(rofs_t *fs)
{
    uint32_t block;
    uint32_t magic;
    int err;

    if (fs == NULL || fs->readonly) {
        return ROFS_ERR_READONLY;
    }

    for (block = 0; block < fs->block_count; block++) {
        /* 只读 4 字节判断是否为 0xFF 空闲块，避免整块读取 */
        err = rofs__read(fs, rofs__blk_addr(fs, block), &magic, 4u);
        if (err != ROFS_OK) {
            return err;
        }
        if (magic != 0xFFFFFFFFu) {
            continue;
        }

        err = rofs__erase(fs, rofs__blk_addr(fs, block), fs->block_size);
        if (err != ROFS_OK) {
            return err;
        }
        err = rofs__blk_write_hdr(fs, block, fs->seq++, 1u);
        if (err != ROFS_OK) {
            return err;
        }

        fs->active_block = block;
        fs->active_off = ROFS_BLK_HDR_SIZE;
        fs->erase_count = 1u;
        if (fs->free_blocks > 0u) {
            fs->free_blocks--;
        }
        fs->used_bytes += fs->block_size;
        return ROFS_OK;
    }

    return ROFS_ERR_NOSPC;
}
