/*
 * rofs_record.c - Record 写入、读取、CRC 校验与日志扫描
 * ============================================================================
 *
 * 物理格式（所有字段小端）：
 *
 *   偏移 0        Record Header (32B)
 *      +0  magic         "REC1"
 *      +4  sequence      全局单调递增
 *      +8  tx_id         事务号（V1 每次操作一个）
 *      +12 object_id     Inode 编号
 *      +16 type          Record 类型
 *      +18 flags         保留
 *      +20 payload_size  Payload 字节数
 *      +24 payload_crc   Payload CRC32
 *      +28 header_crc    前 28B 的 CRC32
 *
 *   偏移 32       Payload
 *   偏移 X        Padding（保持 0xFF）
 *   偏移 M        Commit Marker（4B，最后一次单独写入）
 *
 * 其中 M = align_up(record_start + 32 + payload_size, align)，
 * align = max(device.program_size, 4)，保证 Marker 落在独立编程单元。
 *
 * 掉电安全：
 *   - Header/Payload 写完后先 sync；
 *   - 再单独写 Commit Marker 并 sync；
 *   - 扫描时缺少 Marker 或任一 CRC 错误的记录一律视为无效尾部。
 */

#include "rofs_internal.h"

/*
 * Marker 对齐粒度。
 * 至少 4 字节（u32 访问），并按底层 program_size 向上取整；
 * 上限 256 防止异常配置导致记录无法落块。
 */
static uint32_t rec_align(rofs_t *fs)
{
    uint32_t align = fs->dev.program_size;

    if (align < 4u) {
        align = 4u;
    }
    if (align > 256u) {
        align = 256u;
    }
    return align;
}

/* 把内存中的 Record Header 序列化为 32B 小端字节流，并填入 header_crc */
static void rec_hdr_serialize(const rofs_rec_hdr_t *hdr, uint8_t *buf)
{
    rofs__put_u32(&buf[0], hdr->magic);
    rofs__put_u32(&buf[4], hdr->sequence);
    rofs__put_u32(&buf[8], hdr->tx_id);
    rofs__put_u32(&buf[12], hdr->object_id);
    rofs__put_u16(&buf[16], hdr->type);
    rofs__put_u16(&buf[18], hdr->flags);
    rofs__put_u32(&buf[20], hdr->payload_size);
    rofs__put_u32(&buf[24], hdr->payload_crc);
    rofs__put_u32(&buf[28], rofs__crc32(buf, 28u));
}

/* 解析 32B 小端字节流为 Record Header；CRC 校验由调用者完成 */
static void rec_hdr_parse(const uint8_t *buf, rofs_rec_hdr_t *hdr)
{
    hdr->magic = rofs__get_u32(&buf[0]);
    hdr->sequence = rofs__get_u32(&buf[4]);
    hdr->tx_id = rofs__get_u32(&buf[8]);
    hdr->object_id = rofs__get_u32(&buf[12]);
    hdr->type = rofs__get_u16(&buf[16]);
    hdr->flags = rofs__get_u16(&buf[18]);
    hdr->payload_size = rofs__get_u32(&buf[20]);
    hdr->payload_crc = rofs__get_u32(&buf[24]);
    hdr->header_crc = rofs__get_u32(&buf[28]);
}

/*
 * 流式校验 Payload CRC。
 * 记录 Payload 最大 276B（DATA 前缀 + 256B 数据），
 * 为避免额外 512B 缓冲，这里分块读入 fs->workbuf 计算。
 */
static int rec_check_payload(rofs_t *fs, uint32_t addr, uint32_t size,
                             uint32_t expect_crc)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t left = size;
    uint32_t chunk;
    int err;

    if (fs->workbuf_size == 0u) {
        return ROFS_ERR_PARAM;
    }

    while (left > 0u) {
        chunk = left;
        if (chunk > fs->workbuf_size) {
            chunk = fs->workbuf_size;
        }
        err = rofs__read(fs, addr, fs->workbuf, chunk);
        if (err != ROFS_OK) {
            return err;
        }
        crc = rofs__crc32_update(crc, fs->workbuf, chunk);
        addr += chunk;
        left -= chunk;
    }

    crc ^= 0xFFFFFFFFu;
    return (crc == expect_crc) ? ROFS_OK : ROFS_ERR_CRC;
}

/*
 * 追加写一条 Record。
 *
 * 参数：
 *   type      记录类型；
 *   object_id 关联的 Inode 编号；
 *   p1/l1     第一段 Payload；
 *   p2/l2     第二段 Payload（可为空），两段连续计算 CRC。
 *
 * 流程：
 *   1. 若当前块放不下整条记录，切换到新块；
 *   2. 计算 Payload CRC 与 Header CRC；
 *   3. 写 Header、Payload，sync；
 *   4. 在独立对齐位置写 Commit Marker，sync；
 *   5. 更新 active_off / seq / used_bytes。
 *
 * 任何一步失败都会返回错误；此时记录没有 Marker，挂载时会被忽略。
 */
int rofs__rec_write(rofs_t *fs, uint16_t type, uint32_t object_id,
                    const void *p1, uint32_t l1,
                    const void *p2, uint32_t l2)
{
    rofs_rec_hdr_t hdr;
    uint8_t hb[ROFS_REC_HDR_SIZE];
    uint8_t marker[4];
    uint32_t payload_size = l1 + l2;
    uint32_t align;
    uint32_t marker_off;
    uint32_t end_off;
    uint32_t base;
    uint32_t crc;
    int err;

    if (fs == NULL || !fs->mounted || fs->readonly) {
        return ROFS_ERR_STATE;
    }
    if ((p1 == NULL && l1 > 0u) || (p2 == NULL && l2 > 0u)) {
        return ROFS_ERR_PARAM;
    }
    if (l1 > 0xFFFFFFFFu - l2) {
        return ROFS_ERR_PARAM;
    }

    align = rec_align(fs);

    /* 找到能容纳 Header + Payload + 对齐 Padding + Marker 的块 */
    for (;;) {
        if (fs->active_block == ROFS_BLOCK_NONE) {
            err = rofs__blk_alloc(fs);
            if (err != ROFS_OK) {
                return err;
            }
        }

        marker_off = rofs__align_up(fs->active_off + ROFS_REC_HDR_SIZE +
                                    payload_size, align);
        end_off = marker_off + 4u;

        if (end_off <= fs->block_size) {
            break;
        }

        /* 当前块剩余空间不足：放弃块尾，下次写入时分配新块 */
        if (fs->active_off == ROFS_BLK_HDR_SIZE) {
            return ROFS_ERR_NOSPC;       /* 单条记录超过一个 Block */
        }
        fs->active_block = ROFS_BLOCK_NONE;
    }

    /* 跨两段连续计算 Payload CRC */
    crc = 0xFFFFFFFFu;
    crc = rofs__crc32_update(crc, p1, l1);
    crc = rofs__crc32_update(crc, p2, l2);
    crc ^= 0xFFFFFFFFu;

    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = ROFS_REC_MAGIC;
    hdr.sequence = fs->seq;
    hdr.tx_id = fs->next_tx;
    hdr.object_id = object_id;
    hdr.type = type;
    hdr.payload_size = payload_size;
    hdr.payload_crc = crc;
    rec_hdr_serialize(&hdr, hb);

    base = rofs__blk_addr(fs, fs->active_block);

    /* 1) Header */
    err = rofs__prog(fs, base + fs->active_off, hb, sizeof(hb));
    /* 2) Payload 第一段 */
    if (err == ROFS_OK) {
        err = rofs__prog(fs, base + fs->active_off + ROFS_REC_HDR_SIZE, p1, l1);
    }
    /* 3) Payload 第二段 */
    if (err == ROFS_OK) {
        err = rofs__prog(fs, base + fs->active_off + ROFS_REC_HDR_SIZE + l1,
                         p2, l2);
    }
    /* 4) 先把数据落盘 */
    if (err == ROFS_OK) {
        err = rofs__sync(fs);
    }
    if (err != ROFS_OK) {
        return err;
    }

    /* 5) 最后写入提交标记，这一步完成才代表记录生效 */
    rofs__put_u32(marker, ROFS_CMT_MAGIC);
    err = rofs__prog(fs, base + marker_off, marker, sizeof(marker));
    if (err != ROFS_OK) {
        return err;
    }
    err = rofs__sync(fs);
    if (err != ROFS_OK) {
        return err;
    }

    fs->used_bytes += end_off - fs->active_off;
    fs->active_off = end_off;
    fs->seq++;
    return ROFS_OK;
}

/*
 * 读取并校验指定地址的 Record。
 *
 * 校验顺序：
 *   magic -> header_crc -> marker 边界 -> Commit Marker -> payload_crc。
 * 任一步失败都返回错误，调用者应停止在该块继续扫描。
 *
 * payload 非空时把完整 Payload 读入缓冲区；
 * payload 为空时只做流式 CRC 校验。
 */
int rofs__rec_read(rofs_t *fs, uint32_t rec_addr, rofs_rec_hdr_t *hdr,
                   void *payload, uint32_t cap, uint32_t *next_off)
{
    uint8_t hb[ROFS_REC_HDR_SIZE];
    uint8_t marker[4];
    uint32_t block;
    uint32_t base;
    uint32_t off;
    uint32_t align;
    uint32_t marker_off;
    uint32_t crc;
    int err;

    if (fs == NULL || hdr == NULL || rec_addr < fs->pool_start) {
        return ROFS_ERR_PARAM;
    }

    /* 由绝对地址反推块号与块内偏移 */
    block = (rec_addr - fs->pool_start) / fs->block_size;
    if (block >= fs->block_count) {
        return ROFS_ERR_PARAM;
    }
    base = rofs__blk_addr(fs, block);
    off = rec_addr - base;
    if (off + ROFS_REC_HDR_SIZE > fs->block_size) {
        return ROFS_ERR_PARAM;
    }

    err = rofs__read(fs, rec_addr, hb, sizeof(hb));
    if (err != ROFS_OK) {
        return err;
    }
    rec_hdr_parse(hb, hdr);

    if (hdr->magic != ROFS_REC_MAGIC) {
        return ROFS_ERR_NOENT;           /* 0xFF 或非记录数据 */
    }
    crc = rofs__crc32(hb, 28u);
    if (crc != hdr->header_crc) {
        return ROFS_ERR_CRC;
    }

    /* Marker 必须位于按编程粒度对齐的位置，且在块内 */
    align = rec_align(fs);
    marker_off = rofs__align_up(off + ROFS_REC_HDR_SIZE + hdr->payload_size,
                                align);
    if (marker_off + 4u > fs->block_size) {
        return ROFS_ERR_CRC;
    }

    err = rofs__read(fs, base + marker_off, marker, sizeof(marker));
    if (err != ROFS_OK) {
        return err;
    }
    if (rofs__get_u32(marker) != ROFS_CMT_MAGIC) {
        return ROFS_ERR_CRC;             /* 未提交记录（掉电或写失败） */
    }

    if (payload != NULL) {
        if (hdr->payload_size > cap) {
            return ROFS_ERR_PARAM;
        }
        err = rofs__read(fs, rec_addr + ROFS_REC_HDR_SIZE, payload,
                         hdr->payload_size);
        if (err != ROFS_OK) {
            return err;
        }
        crc = rofs__crc32(payload, hdr->payload_size);
        if (crc != hdr->payload_crc) {
            return ROFS_ERR_CRC;
        }
    } else {
        err = rec_check_payload(fs, rec_addr + ROFS_REC_HDR_SIZE,
                                hdr->payload_size, hdr->payload_crc);
        if (err != ROFS_OK) {
            return err;
        }
    }

    if (next_off != NULL) {
        *next_off = base + marker_off + 4u;
    }
    return ROFS_OK;
}

/*
 * 直接读取 Payload 的指定区间。
 * 不做 CRC/边界之外的重复校验，调用者应已通过 rofs__rec_read 确认记录有效。
 */
int rofs__rec_read_raw(rofs_t *fs, uint32_t rec_addr,
                       const rofs_rec_hdr_t *hdr, uint32_t payload_off,
                       void *dst, uint32_t len)
{
    if (fs == NULL || hdr == NULL || dst == NULL) {
        return ROFS_ERR_PARAM;
    }
    if (payload_off > hdr->payload_size ||
        len > hdr->payload_size - payload_off) {
        return ROFS_ERR_PARAM;
    }
    if (len == 0u) {
        return ROFS_OK;
    }
    return rofs__read(fs, rec_addr + ROFS_REC_HDR_SIZE + payload_off, dst, len);
}

/*
 * 扫描一个块。
 *
 * 从 Block Header 之后开始逐条验证 Record：
 *   - magic 不对或记录校验失败：停止，认为到达块尾；
 *   - 否则回调通知上层，并继续下一条。
 *
 * 这种“遇到第一条无效记录即停止”的策略符合追加写模型：
 * 掉电只可能破坏尾部，后文不会再有有效记录。
 *
 * 返回 last_off：最后一条有效记录之后的块内偏移，用于设置 active_off。
 */
int rofs__scan_block(rofs_t *fs, uint32_t block, rofs_rec_cb_t cb,
                     void *ctx, uint32_t *last_off)
{
    rofs_blk_hdr_t bh;
    rofs_rec_hdr_t hdr;
    uint32_t base;
    uint32_t off;
    uint32_t next;
    int err;

    if (fs == NULL || block >= fs->block_count) {
        return ROFS_ERR_PARAM;
    }

    /* 先确认 Block Header 有效；无效块由调用者按空闲块处理 */
    err = rofs__blk_read_hdr(fs, block, &bh);
    if (err != ROFS_OK) {
        return err;
    }

    base = rofs__blk_addr(fs, block);
    off = ROFS_BLK_HDR_SIZE;

    while (off + ROFS_REC_HDR_SIZE <= fs->block_size) {
        err = rofs__rec_read(fs, base + off, &hdr, NULL, 0u, &next);
        if (err != ROFS_OK) {
            break;                       /* 未提交/损坏：块尾结束 */
        }
        if (next <= base + off || next > base + fs->block_size) {
            break;                       /* 防御异常尺寸导致的死循环 */
        }
        if (cb != NULL) {
            err = cb(fs, &hdr, base + off, ctx);
            if (err != ROFS_OK) {
                return err;
            }
        }
        off = next - base;
    }

    if (last_off != NULL) {
        *last_off = off;
    }
    return ROFS_OK;
}

/*
 * 扫描整个数据池。
 * 空闲块（Header 无效）直接跳过；块头 CRC 错误也按空闲处理，
 * 避免单块损坏导致整个文件系统无法挂载。
 */
int rofs__scan_all(rofs_t *fs, rofs_rec_cb_t cb, void *ctx)
{
    uint32_t block;
    uint32_t last;
    int err;

    if (fs == NULL) {
        return ROFS_ERR_PARAM;
    }

    for (block = 0; block < fs->block_count; block++) {
        err = rofs__scan_block(fs, block, cb, ctx, &last);
        if (err == ROFS_OK || err == ROFS_ERR_NOENT || err == ROFS_ERR_CRC) {
            continue;
        }
        return err;
    }
    return ROFS_OK;
}
