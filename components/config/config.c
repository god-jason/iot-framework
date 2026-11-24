/*
 * config.c - 双区轮转、追加日志式配置存储（与平台无关）
 *
 * 记录在 Flash 中的布局（长度单位：字节）：
 *   [记录头 20B][数据 Len B][填充][Commit 4B][填充]
 *   - 记录头：magic/version/length/sequence/crc32/reserved；
 *   - Commit 位于按 program_size 对齐的位置，且是整条记录最后写入的 4 字节；
 *   - 记录总长按 program_size 向上对齐，保证下一条记录起始地址仍对齐。
 *
 * 有效性 = Commit == CONFIG_STORE_COMMIT 且 CRC32(记录头(CRC 字段按 0)+数据) 匹配。
 *
 * 追加与轮转：
 *   - 只在“当前活动区”内追加；magic 为擦除态视为空尾，可直接续写；
 *   - 遇到非法头或空间不足时，擦除另一区并从偏移 0 写入新记录；
 *   - 轮转前活动区里始终保留最后一条有效记录（另一区只会更旧），
 *     因此任何时刻至少有一份完整配置可恢复。
 *
 * 本文件不使用 libc（可编入 -nostdlib 的 Bootloader），也不使用动态内存。
 */

#include "config.h"

#include <stddef.h>

#define REC_HDR_SIZE        ((uint32_t)sizeof(config_store_hdr_t))
#define REC_COMMIT_SIZE     4U
#define REC_MIN_SIZE        (REC_HDR_SIZE + REC_COMMIT_SIZE)
#define SCAN_CHUNK_SIZE     32U

/* CRC32（IEEE 802.3，反射多项式 0xEDB88320）nibble 表：
 * 16 项 × 4B，比 256 项表省 960B Flash。 */
static const uint32_t s_crc32_tab[16] = {
    0x00000000U, 0x1DB71064U, 0x3B6E20C8U, 0x26D930ACU,
    0x76DC4190U, 0x6B6B51F4U, 0x4DB26158U, 0x5005713CU,
    0xEDB88320U, 0xF00F9344U, 0xD6D6A3E8U, 0xCB61B38CU,
    0x9B64C2B0U, 0x86D3D2D4U, 0xA00AE278U, 0xBDBDF21CU
};

static uint32_t crc32_push(uint32_t crc, uint8_t byte)
{
    crc ^= (uint32_t)byte;
    crc = (crc >> 4) ^ s_crc32_tab[crc & 0x0FU];
    crc = (crc >> 4) ^ s_crc32_tab[crc & 0x0FU];
    return crc;
}

static uint32_t crc32_push_buf(uint32_t crc, const void *buf, uint32_t size)
{
    const uint8_t *p = (const uint8_t *)buf;

    while (size > 0U) {
        crc = crc32_push(crc, *p);
        p++;
        size--;
    }
    return crc;
}

/* 计算记录头 CRC（crc32 字段按 0 参与），不含终结异或，便于继续累加数据 */
static uint32_t crc32_hdr_raw(const config_store_hdr_t *hdr)
{
    static const uint8_t zero[4] = { 0U, 0U, 0U, 0U };
    const uint8_t *p = (const uint8_t *)hdr;
    uint32_t crc_off = (uint32_t)offsetof(config_store_hdr_t, crc32);
    uint32_t crc = 0xFFFFFFFFU;

    crc = crc32_push_buf(crc, p, crc_off);
    crc = crc32_push_buf(crc, zero, 4U);
    crc = crc32_push_buf(crc, p + crc_off + 4U, REC_HDR_SIZE - crc_off - 4U);
    return crc;
}

static uint32_t align_up(uint32_t value, uint32_t align)
{
    return ((value + align - 1U) / align) * align;
}

/* 记录对齐粒度：不小于 4 字节，保证 Commit 与下一条目地址对齐 */
static uint32_t record_align(const config_store_layout_t *layout)
{
    return (layout->program_size < 4U) ? 4U : layout->program_size;
}

/* Commit 在记录内的偏移：跳过记录头与按粒度对齐的数据区 */
static uint32_t record_commit_off(const config_store_layout_t *layout,
                                  uint32_t length)
{
    return REC_HDR_SIZE + align_up(length, record_align(layout));
}

/* 整条记录占用（含尾部对齐） */
static uint32_t record_size(const config_store_layout_t *layout, uint32_t length)
{
    return align_up(record_commit_off(layout, length) + REC_COMMIT_SIZE,
                    record_align(layout));
}

/* uint32_t 序列号比较：有符号差值处理回绕 */
static int seq_newer(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

/* ---- 底层读写 ---- */

static int flash_read(const config_store_t *store, uint32_t addr,
                      void *buf, uint32_t size)
{
    if (size == 0U) {
        return 0;
    }
    return store->ops->read(addr, buf, size);
}

static int flash_write(const config_store_t *store, uint32_t addr,
                       const void *buf, uint32_t size)
{
    if (size == 0U) {
        return 0;
    }
    return store->ops->write(addr, buf, size);
}

/* 记录头是否合理：magic 正确、长度不超限、整条记录落在扇区内 */
static int hdr_sane(const config_store_t *store, uint32_t offset,
                    const config_store_hdr_t *hdr)
{
    const config_store_layout_t *layout = store->layout;

    if (hdr->magic != CONFIG_STORE_MAGIC) {
        return 0;
    }
    if ((uint32_t)hdr->length > layout->max_data_size) {
        return 0;
    }
    if (offset + record_size(layout, (uint32_t)hdr->length) > layout->sector_size) {
        return 0;
    }
    return 1;
}

/* 校验记录的数据部分 CRC（分块读取，避免整条记录进栈） */
static int record_crc_ok(const config_store_t *store, uint32_t record_addr,
                         const config_store_hdr_t *hdr)
{
    uint8_t chunk[SCAN_CHUNK_SIZE];
    uint32_t crc = crc32_hdr_raw(hdr);
    uint32_t addr = record_addr + REC_HDR_SIZE;
    uint32_t left = (uint32_t)hdr->length;

    while (left > 0U) {
        uint32_t n = (left > SCAN_CHUNK_SIZE) ? SCAN_CHUNK_SIZE : left;

        if (flash_read(store, addr, chunk, n) != 0) {
            return 0;
        }
        crc = crc32_push_buf(crc, chunk, n);
        addr += n;
        left -= n;
    }
    return (crc ^ 0xFFFFFFFFU) == hdr->crc32;
}

/* 记录是否有效：Commit 已写入且 CRC 匹配 */
static int record_valid(const config_store_t *store, uint32_t record_addr,
                        const config_store_hdr_t *hdr)
{
    uint32_t commit = 0U;
    uint32_t commit_addr = record_addr +
                           record_commit_off(store->layout,
                                             (uint32_t)hdr->length);

    if (flash_read(store, commit_addr, &commit, REC_COMMIT_SIZE) != 0) {
        return 0;
    }
    if (commit != CONFIG_STORE_COMMIT) {
        return 0;                       /* 未提交：掉电/写失败后的半成品 */
    }
    return record_crc_ok(store, record_addr, hdr);
}

/* 单个扇区的扫描结果 */
typedef struct {
    uint32_t write_offset;      /* 下一条记录可用的追加偏移（=sector_size 表示已满） */
    uint32_t sequence;          /* 区内最新有效记录序号 */
    uint32_t newest_addr;       /* 区内最新有效记录绝对地址 */
    uint32_t count;             /* 区内有效记录数 */
    uint16_t version;
    uint16_t length;
    uint8_t  has;               /* 是否存在有效记录 */
} area_info_t;

static int scan_area(const config_store_t *store, uint32_t index,
                     area_info_t *info)
{
    const config_store_layout_t *layout = store->layout;
    uint32_t base = layout->sector_addr[index];
    uint32_t offset = 0U;
    uint8_t full = 0U;

    info->write_offset = 0U;
    info->sequence = 0U;
    info->newest_addr = 0U;
    info->count = 0U;
    info->version = 0U;
    info->length = 0U;
    info->has = 0U;

    while (offset + REC_MIN_SIZE <= layout->sector_size) {
        config_store_hdr_t hdr;

        if (flash_read(store, base + offset, &hdr, REC_HDR_SIZE) != 0) {
            return CONFIG_STORE_ERR_IO;
        }
        if (hdr.magic == CONFIG_STORE_ERASED) {
            break;                      /* 空尾：可在该偏移继续追加 */
        }
        if (!hdr_sane(store, offset, &hdr)) {
            full = 1U;                  /* 记录头损坏：本区不再追加 */
            break;
        }
        if (record_valid(store, base + offset, &hdr)) {
            if (!info->has || seq_newer(hdr.sequence, info->sequence)) {
                info->sequence = hdr.sequence;
                info->newest_addr = base + offset;
                info->version = hdr.version;
                info->length = hdr.length;
                info->has = 1U;
            }
            info->count++;
        }
        /* 未提交/CRC 失败的记录也要跨过，否则会覆盖已编程过的字节 */
        offset += record_size(layout, (uint32_t)hdr.length);
        info->write_offset = offset;
    }
    if (full) {
        info->write_offset = layout->sector_size;
    }
    return CONFIG_STORE_OK;
}

/* ---- 对外接口 ---- */

int config_store_init(config_store_t *store,
                      const config_store_layout_t *layout,
                      const config_store_ops_t *ops)
{
    if (store == NULL || layout == NULL || ops == NULL ||
        ops->read == NULL || ops->write == NULL || ops->erase == NULL) {
        return CONFIG_STORE_ERR_PARAM;
    }
    if (layout->sector_addr[0] == layout->sector_addr[1] ||
        layout->sector_size < REC_MIN_SIZE ||
        layout->max_data_size == 0U || layout->max_data_size > 0xFFFFU ||
        layout->max_data_size + REC_MIN_SIZE > layout->sector_size) {
        return CONFIG_STORE_ERR_PARAM;
    }

    store->layout = layout;
    store->ops = ops;
    store->initialized = 1U;
    return config_store_check(store);
}

int config_store_check(config_store_t *store)
{
    const config_store_layout_t *layout;
    area_info_t a0;
    area_info_t a1;
    const area_info_t *best = NULL;
    uint32_t best_index = 0U;
    int rc;

    if (store == NULL || store->layout == NULL || store->ops == NULL) {
        return CONFIG_STORE_ERR_PARAM;
    }
    layout = store->layout;

    rc = scan_area(store, 0U, &a0);
    if (rc != CONFIG_STORE_OK) {
        return rc;
    }
    rc = scan_area(store, 1U, &a1);
    if (rc != CONFIG_STORE_OK) {
        return rc;
    }

    if (a0.has) {
        best = &a0;
        best_index = 0U;
    }
    if (a1.has && (best == NULL || seq_newer(a1.sequence, best->sequence))) {
        best = &a1;
        best_index = 1U;
    }

    if (best != NULL) {
        store->active_index = best_index;
        store->write_offset = best->write_offset;
        store->sequence = best->sequence;
        store->newest_addr = best->newest_addr;
        store->record_count = best->count;
        store->version = best->version;
        store->length = best->length;
        store->has_record = 1U;
        return CONFIG_STORE_OK;
    }

    /* 尚无有效记录：优先选仍有空位的区，否则标记为满以便下次保存轮转 */
    store->has_record = 0U;
    store->sequence = 0U;
    store->newest_addr = 0U;
    store->record_count = 0U;
    store->version = 0U;
    store->length = 0U;
    if (a0.write_offset < layout->sector_size) {
        store->active_index = 0U;
        store->write_offset = a0.write_offset;
    } else if (a1.write_offset < layout->sector_size) {
        store->active_index = 1U;
        store->write_offset = a1.write_offset;
    } else {
        store->active_index = 0U;
        store->write_offset = layout->sector_size;
    }
    return CONFIG_STORE_NONE;
}

int config_store_load_raw(config_store_t *store, void *buf, uint32_t cap,
                          uint32_t *len, uint16_t *version)
{
    config_store_hdr_t hdr;

    if (store == NULL || store->layout == NULL || store->ops == NULL ||
        (buf == NULL && cap != 0U)) {
        return CONFIG_STORE_ERR_PARAM;
    }
    if (!store->has_record) {
        return CONFIG_STORE_NONE;
    }
    if (cap < (uint32_t)store->length) {
        return CONFIG_STORE_ERR_SIZE;
    }

    if (flash_read(store, store->newest_addr, &hdr, REC_HDR_SIZE) != 0) {
        return CONFIG_STORE_ERR_IO;
    }
    if (!record_valid(store, store->newest_addr, &hdr)) {
        return CONFIG_STORE_ERR_VERIFY;
    }
    if (flash_read(store, store->newest_addr + REC_HDR_SIZE, buf,
                   (uint32_t)store->length) != 0) {
        return CONFIG_STORE_ERR_IO;
    }

    if (len != NULL) {
        *len = (uint32_t)hdr.length;
    }
    if (version != NULL) {
        *version = hdr.version;
    }
    return CONFIG_STORE_OK;
}

int config_store_load(config_store_t *store, void *data, uint32_t size)
{
    if (data == NULL) {
        return CONFIG_STORE_ERR_PARAM;
    }
    if (store == NULL || !store->has_record) {
        return CONFIG_STORE_NONE;
    }
    if (size != (uint32_t)store->length) {
        return CONFIG_STORE_ERR_SIZE;
    }
    return config_store_load_raw(store, data, size, NULL, NULL);
}

int config_store_save(config_store_t *store, const void *data, uint32_t size)
{
    const config_store_layout_t *layout;
    config_store_hdr_t hdr;
    config_store_hdr_t check;
    uint32_t total;
    uint32_t commit_offset;
    uint32_t sector_index;
    uint32_t offset;
    uint32_t base;
    uint32_t commit = CONFIG_STORE_COMMIT;
    uint32_t crc;
    uint8_t rotated = 0U;

    if (store == NULL || !store->initialized ||
        store->layout == NULL || store->ops == NULL) {
        return CONFIG_STORE_ERR_PARAM;
    }
    if (data == NULL && size != 0U) {
        return CONFIG_STORE_ERR_PARAM;
    }
    layout = store->layout;
    if (size > layout->max_data_size) {
        return CONFIG_STORE_ERR_SIZE;
    }

    total = record_size(layout, size);
    commit_offset = record_commit_off(layout, size);
    sector_index = store->active_index;
    offset = store->write_offset;

    if (offset + total > layout->sector_size) {
        /* 当前区写满：擦除另一区并从 0 开始。
         * 另一区只会包含更旧的记录，当前区的最新有效记录仍然保留。 */
        uint32_t other = sector_index ^ 1U;

        if (store->ops->erase(layout->sector_addr[other],
                              layout->sector_size) != 0) {
            return CONFIG_STORE_ERR_IO;
        }
        sector_index = other;
        offset = 0U;
        rotated = 1U;
    }
    base = layout->sector_addr[sector_index] + offset;

    hdr.magic = CONFIG_STORE_MAGIC;
    hdr.version = layout->version;
    hdr.length = (uint16_t)size;
    hdr.sequence = store->sequence + 1U;
    hdr.crc32 = 0U;
    hdr.reserved = CONFIG_STORE_ERASED;
    crc = crc32_push_buf(crc32_hdr_raw(&hdr), data, size);
    hdr.crc32 = crc ^ 0xFFFFFFFFU;

    /* 1) 记录头  2) 数据  3) 回读校验  4) 最后写 Commit */
    if (flash_write(store, base, &hdr, REC_HDR_SIZE) != 0 ||
        flash_write(store, base + REC_HDR_SIZE, data, size) != 0) {
        goto save_failed;
    }

    if (flash_read(store, base, &check, REC_HDR_SIZE) != 0 ||
        check.magic != hdr.magic ||
        check.version != hdr.version ||
        check.length != hdr.length ||
        check.sequence != hdr.sequence ||
        check.crc32 != hdr.crc32 ||
        !record_crc_ok(store, base, &check)) {
        goto save_failed;
    }

    if (flash_write(store, base + commit_offset, &commit, REC_COMMIT_SIZE) != 0) {
        goto save_failed;
    }

    store->active_index = sector_index;
    store->write_offset = offset + total;
    store->sequence = hdr.sequence;
    store->newest_addr = base;
    store->version = hdr.version;
    store->length = hdr.length;
    store->has_record = 1U;
    store->record_count = rotated ? 1U : (store->record_count + 1U);
    return CONFIG_STORE_OK;

save_failed:
    /* 写失败后状态未知（可能已轮转），重新扫描避免后续误擦唯一有效记录 */
    (void)config_store_check(store);
    return CONFIG_STORE_ERR_IO;
}

int config_store_format(config_store_t *store)
{
    uint32_t i;

    if (store == NULL || store->layout == NULL || store->ops == NULL) {
        return CONFIG_STORE_ERR_PARAM;
    }
    if (store->layout->sector_addr[0] == store->layout->sector_addr[1]) {
        return CONFIG_STORE_ERR_PARAM;
    }

    for (i = 0U; i < 2U; i++) {
        if (store->ops->erase(store->layout->sector_addr[i],
                              store->layout->sector_size) != 0) {
            (void)config_store_check(store);
            return CONFIG_STORE_ERR_IO;
        }
    }

    store->active_index = 0U;
    store->write_offset = 0U;
    store->sequence = 0U;
    store->newest_addr = 0U;
    store->record_count = 0U;
    store->version = 0U;
    store->length = 0U;
    store->has_record = 0U;
    return CONFIG_STORE_OK;
}
