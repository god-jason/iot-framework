/*
 * mfs_record.c - Record 写入、读取、CRC 校验与日志扫描
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

#include "mfs_internal.h"

/*
 * Marker 对齐粒度。
 * 至少 4 字节（u32 访问），并按底层 program_size 向上取整；
 * 上限 256 防止异常配置导致记录无法落块。
 */
static uint32_t rec_align(mfs_t *fs)
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
static void rec_hdr_serialize(const mfs_rec_hdr_t *hdr, uint8_t *buf)
{
    mfs__put_u32(&buf[0], hdr->magic);
    mfs__put_u32(&buf[4], hdr->sequence);
    mfs__put_u32(&buf[8], hdr->tx_id);
    mfs__put_u32(&buf[12], hdr->object_id);
    mfs__put_u16(&buf[16], hdr->type);
    mfs__put_u16(&buf[18], hdr->flags);
    mfs__put_u32(&buf[20], hdr->payload_size);
    mfs__put_u32(&buf[24], hdr->payload_crc);
    mfs__put_u32(&buf[28], mfs__crc32(buf, 28u));
}

/* 解析 32B 小端字节流为 Record Header；CRC 校验由调用者完成 */
static void rec_hdr_parse(const uint8_t *buf, mfs_rec_hdr_t *hdr)
{
    hdr->magic = mfs__get_u32(&buf[0]);
    hdr->sequence = mfs__get_u32(&buf[4]);
    hdr->tx_id = mfs__get_u32(&buf[8]);
    hdr->object_id = mfs__get_u32(&buf[12]);
    hdr->type = mfs__get_u16(&buf[16]);
    hdr->flags = mfs__get_u16(&buf[18]);
    hdr->payload_size = mfs__get_u32(&buf[20]);
    hdr->payload_crc = mfs__get_u32(&buf[24]);
    hdr->header_crc = mfs__get_u32(&buf[28]);
}

/*
 * 流式校验 Payload CRC。
 * 记录 Payload 最大 276B（DATA 前缀 + 256B 数据），
 * 为避免额外 512B 缓冲，这里分块读入 fs->workbuf 计算。
 */
static int rec_check_payload(mfs_t *fs, uint32_t addr, uint32_t size,
                             uint32_t expect_crc)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t left = size;
    uint32_t chunk;
    int err;

    if (fs->workbuf_size == 0u) {
        return MFS_ERR_PARAM;
    }

    while (left > 0u) {
        chunk = left;
        if (chunk > fs->workbuf_size) {
            chunk = fs->workbuf_size;
        }
        err = mfs__read(fs, addr, fs->workbuf, chunk);
        if (err != MFS_OK) {
            return err;
        }
        crc = mfs__crc32_update(crc, fs->workbuf, chunk);
        addr += chunk;
        left -= chunk;
    }

    crc ^= 0xFFFFFFFFu;
    return (crc == expect_crc) ? MFS_OK : MFS_ERR_CRC;
}

/* 计算一条记录占用的总字节数，供 GC 统计块内有效数据量 */
uint32_t mfs__rec_total_size(mfs_t *fs, uint32_t rec_off,
                              uint32_t payload_size)
{
    uint32_t align = rec_align(fs);
    uint32_t marker_off = mfs__align_up(rec_off + MFS_REC_HDR_SIZE +
                                         payload_size, align);

    return marker_off + 4u - rec_off;
}

/* ---------------- DATA 记录定位 ---------------- */

/*
 * 定位回调。
 * 同时统计两类记录：
 *   - DATA        ：找出 generation 最大的有效数据；
 *   - DATA_DELETE ：记录覆盖该 Chunk 的最新失效序号。
 * 最终有效性 = DATA.seq > DATA_DELETE.seq。
 */
static int data_locate_cb(mfs_t *fs, const mfs_rec_hdr_t *hdr,
                          uint32_t rec_addr, void *ctx)
{
    mfs_data_loc_t *loc = (mfs_data_loc_t *)ctx;
    uint8_t buf[MFS_DATA_PREFIX];
    int err;

    /* 只关心同一 Inode 的记录 */
    if (hdr->object_id != loc->inode) {
        return MFS_OK;
    }

    /* --- DATA_DELETE：范围失效 --- */
    if (hdr->type == MFS_REC_DATA_DELETE) {
        uint32_t start;
        uint32_t count;

        if (hdr->payload_size < 16u) {
            return MFS_OK;
        }
        err = mfs__rec_read_raw(fs, rec_addr, hdr, 0u, buf, 16u);
        if (err != MFS_OK) {
            return err;
        }
        if (mfs__get_u32(&buf[12]) != loc->epoch) {
            return MFS_OK;              /* 不同 epoch 的删除不影响当前数据 */
        }
        start = mfs__get_u32(&buf[4]);
        count = mfs__get_u32(&buf[8]);
        if (loc->chunk >= start && loc->chunk - start < count &&
            hdr->sequence > loc->del_seq) {
            loc->del_seq = hdr->sequence;
        }
        return MFS_OK;
    }

    /* --- DATA：找最大 generation --- */
    if (hdr->type != MFS_REC_DATA ||
        hdr->payload_size < MFS_DATA_PREFIX) {
        return MFS_OK;
    }
    err = mfs__rec_read_raw(fs, rec_addr, hdr, 0u, buf, sizeof(buf));
    if (err != MFS_OK) {
        return err;
    }
    if (mfs__get_u32(&buf[4]) != loc->chunk ||
        mfs__get_u32(&buf[12]) != loc->epoch) {
        return MFS_OK;
    }
    if (mfs__get_u16(&buf[16]) > MFS_CHUNK_SIZE) {
        return MFS_OK;
    }
    {
        uint32_t gen = mfs__get_u32(&buf[8]);

        /* generation 大者优先；相同时取后写入者 */
        if (!loc->found || gen > loc->gen ||
            (gen == loc->gen && hdr->sequence > loc->seq)) {
            loc->found = 1u;
            loc->gen = gen;
            loc->seq = hdr->sequence;
            loc->addr = rec_addr;
            loc->size = mfs__get_u16(&buf[16]);
        }
    }
    return MFS_OK;
}

/*
 * 定位指定 (inode, chunk, epoch) 的最新 DATA 与覆盖它的 DATA_DELETE。
 * 返回 MFS_OK 表示扫描完成（loc->found 才代表存在数据）。
 */
int mfs__data_locate(mfs_t *fs, uint32_t inode, uint32_t chunk,
                      uint32_t epoch, mfs_data_loc_t *loc)
{
    if (fs == NULL || loc == NULL) {
        return MFS_ERR_PARAM;
    }

    memset(loc, 0, sizeof(*loc));
    loc->inode = inode;
    loc->chunk = chunk;
    loc->epoch = epoch;

    return mfs__scan_all(fs, data_locate_cb, loc);
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
 * 空间不足且开启 auto_gc 时，会先触发一次强制 GC 再重试分配。
 * GC 自身写记录时 gc_running=1，不会递归触发。
 *
 * 任何一步失败都会返回错误；此时记录没有 Marker，挂载时会被忽略。
 */
int mfs__rec_write(mfs_t *fs, uint16_t type, uint32_t object_id,
                    const void *p1, uint32_t l1,
                    const void *p2, uint32_t l2)
{
    mfs_rec_hdr_t hdr;
    uint8_t hb[MFS_REC_HDR_SIZE];
    uint8_t marker[4];
    uint32_t payload_size = l1 + l2;
    uint32_t align;
    uint32_t marker_off;
    uint32_t end_off;
    uint32_t base;
    uint32_t crc;
    int err;

    if (fs == NULL || !fs->mounted || fs->readonly) {
        return MFS_ERR_STATE;
    }
    if ((p1 == NULL && l1 > 0u) || (p2 == NULL && l2 > 0u)) {
        return MFS_ERR_PARAM;
    }
    if (l1 > 0xFFFFFFFFu - l2) {
        return MFS_ERR_PARAM;
    }

    align = rec_align(fs);

    /* 找到能容纳 Header + Payload + 对齐 Padding + Marker 的块 */
    for (;;) {
        if (fs->active_block == MFS_BLOCK_NONE) {
            err = mfs__blk_alloc(fs);
            if (err == MFS_ERR_NOSPC && fs->cfg.auto_gc &&
                !fs->gc_running) {
                /* 没有空闲块：尝试回收一个块后再分配 */
                err = mfs__gc_run(fs, 1u);
                if (err == MFS_OK) {
                    err = mfs__blk_alloc(fs);
                }
            }
            if (err != MFS_OK) {
                return err;
            }
        }

        marker_off = mfs__align_up(fs->active_off + MFS_REC_HDR_SIZE +
                                    payload_size, align);
        end_off = marker_off + 4u;

        if (end_off <= fs->block_size) {
            break;
        }

        /* 当前块剩余空间不足：放弃块尾，下次写入时分配新块 */
        if (fs->active_off == MFS_BLK_HDR_SIZE) {
            return MFS_ERR_NOSPC;       /* 单条记录超过一个 Block */
        }
        fs->active_block = MFS_BLOCK_NONE;
    }

    /* 跨两段连续计算 Payload CRC */
    crc = 0xFFFFFFFFu;
    crc = mfs__crc32_update(crc, p1, l1);
    crc = mfs__crc32_update(crc, p2, l2);
    crc ^= 0xFFFFFFFFu;

    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = MFS_REC_MAGIC;
    hdr.sequence = fs->seq;
    hdr.tx_id = fs->next_tx;
    hdr.object_id = object_id;
    hdr.type = type;
    hdr.payload_size = payload_size;
    hdr.payload_crc = crc;
    rec_hdr_serialize(&hdr, hb);

    base = mfs__blk_addr(fs, fs->active_block);

    /* 1) Header */
    err = mfs__prog(fs, base + fs->active_off, hb, sizeof(hb));
    /* 2) Payload 第一段 */
    if (err == MFS_OK) {
        err = mfs__prog(fs, base + fs->active_off + MFS_REC_HDR_SIZE, p1, l1);
    }
    /* 3) Payload 第二段 */
    if (err == MFS_OK) {
        err = mfs__prog(fs, base + fs->active_off + MFS_REC_HDR_SIZE + l1,
                         p2, l2);
    }
    /* 4) 先把数据落盘 */
    if (err == MFS_OK) {
        err = mfs__sync(fs);
    }
    if (err != MFS_OK) {
        return err;
    }

    /* 5) 最后写入提交标记，这一步完成才代表记录生效 */
    mfs__put_u32(marker, MFS_CMT_MAGIC);
    err = mfs__prog(fs, base + marker_off, marker, sizeof(marker));
    if (err != MFS_OK) {
        return err;
    }
    err = mfs__sync(fs);
    if (err != MFS_OK) {
        return err;
    }

    fs->used_bytes += end_off - fs->active_off;
    fs->active_off = end_off;
    fs->seq++;
    return MFS_OK;
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
int mfs__rec_read(mfs_t *fs, uint32_t rec_addr, mfs_rec_hdr_t *hdr,
                   void *payload, uint32_t cap, uint32_t *next_off)
{
    uint8_t hb[MFS_REC_HDR_SIZE];
    uint8_t marker[4];
    uint32_t block;
    uint32_t base;
    uint32_t off;
    uint32_t align;
    uint32_t marker_off;
    uint32_t crc;
    int err;

    if (fs == NULL || hdr == NULL || rec_addr < fs->pool_start) {
        return MFS_ERR_PARAM;
    }

    /* 由绝对地址反推块号与块内偏移 */
    block = (rec_addr - fs->pool_start) / fs->block_size;
    if (block >= fs->block_count) {
        return MFS_ERR_PARAM;
    }
    base = mfs__blk_addr(fs, block);
    off = rec_addr - base;
    if (off + MFS_REC_HDR_SIZE > fs->block_size) {
        return MFS_ERR_PARAM;
    }

    err = mfs__read(fs, rec_addr, hb, sizeof(hb));
    if (err != MFS_OK) {
        return err;
    }
    rec_hdr_parse(hb, hdr);

    if (hdr->magic != MFS_REC_MAGIC) {
        return MFS_ERR_NOENT;           /* 0xFF 或非记录数据 */
    }
    crc = mfs__crc32(hb, 28u);
    if (crc != hdr->header_crc) {
        return MFS_ERR_CRC;
    }

    /* Marker 必须位于按编程粒度对齐的位置，且在块内 */
    align = rec_align(fs);
    marker_off = mfs__align_up(off + MFS_REC_HDR_SIZE + hdr->payload_size,
                                align);
    if (marker_off + 4u > fs->block_size) {
        return MFS_ERR_CRC;
    }

    err = mfs__read(fs, base + marker_off, marker, sizeof(marker));
    if (err != MFS_OK) {
        return err;
    }
    if (mfs__get_u32(marker) != MFS_CMT_MAGIC) {
        return MFS_ERR_CRC;             /* 未提交记录（掉电或写失败） */
    }

    if (payload != NULL) {
        if (hdr->payload_size > cap) {
            return MFS_ERR_PARAM;
        }
        err = mfs__read(fs, rec_addr + MFS_REC_HDR_SIZE, payload,
                         hdr->payload_size);
        if (err != MFS_OK) {
            return err;
        }
        crc = mfs__crc32(payload, hdr->payload_size);
        if (crc != hdr->payload_crc) {
            return MFS_ERR_CRC;
        }
    } else {
        err = rec_check_payload(fs, rec_addr + MFS_REC_HDR_SIZE,
                                hdr->payload_size, hdr->payload_crc);
        if (err != MFS_OK) {
            return err;
        }
    }

    if (next_off != NULL) {
        *next_off = base + marker_off + 4u;
    }
    return MFS_OK;
}

/*
 * 直接读取 Payload 的指定区间。
 * 不做 CRC/边界之外的重复校验，调用者应已通过 mfs__rec_read 确认记录有效。
 */
int mfs__rec_read_raw(mfs_t *fs, uint32_t rec_addr,
                       const mfs_rec_hdr_t *hdr, uint32_t payload_off,
                       void *dst, uint32_t len)
{
    if (fs == NULL || hdr == NULL || dst == NULL) {
        return MFS_ERR_PARAM;
    }
    if (payload_off > hdr->payload_size ||
        len > hdr->payload_size - payload_off) {
        return MFS_ERR_PARAM;
    }
    if (len == 0u) {
        return MFS_OK;
    }
    return mfs__read(fs, rec_addr + MFS_REC_HDR_SIZE + payload_off, dst, len);
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
int mfs__scan_block(mfs_t *fs, uint32_t block, mfs_rec_cb_t cb,
                     void *ctx, uint32_t *last_off)
{
    mfs_blk_hdr_t bh;
    mfs_rec_hdr_t hdr;
    uint32_t base;
    uint32_t off;
    uint32_t next;
    int err;

    if (fs == NULL || block >= fs->block_count) {
        return MFS_ERR_PARAM;
    }

    /* 先确认 Block Header 有效；无效块由调用者按空闲块处理 */
    err = mfs__blk_read_hdr(fs, block, &bh);
    if (err != MFS_OK) {
        return err;
    }

    base = mfs__blk_addr(fs, block);
    off = MFS_BLK_HDR_SIZE;

    while (off + MFS_REC_HDR_SIZE <= fs->block_size) {
        err = mfs__rec_read(fs, base + off, &hdr, NULL, 0u, &next);
        if (err != MFS_OK) {
            break;                       /* 未提交/损坏：块尾结束 */
        }
        if (next <= base + off || next > base + fs->block_size) {
            break;                       /* 防御异常尺寸导致的死循环 */
        }
        if (cb != NULL) {
            err = cb(fs, &hdr, base + off, ctx);
            if (err != MFS_OK) {
                return err;
            }
        }
        off = next - base;
    }

    if (last_off != NULL) {
        *last_off = off;
    }
    return MFS_OK;
}

/*
 * 扫描整个数据池。
 * 空闲块（Header 无效）直接跳过；块头 CRC 错误也按空闲处理，
 * 避免单块损坏导致整个文件系统无法挂载。
 */
int mfs__scan_all(mfs_t *fs, mfs_rec_cb_t cb, void *ctx)
{
    uint32_t block;
    uint32_t last;
    int err;

    if (fs == NULL) {
        return MFS_ERR_PARAM;
    }

    for (block = 0; block < fs->block_count; block++) {
        err = mfs__scan_block(fs, block, cb, ctx, &last);
        if (err == MFS_OK || err == MFS_ERR_NOENT || err == MFS_ERR_CRC) {
            continue;
        }
        return err;
    }
    return MFS_OK;
}
