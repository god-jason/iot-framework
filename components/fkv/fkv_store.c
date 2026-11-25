/*
 * fkv_store.c - 键值操作与分区滚动压缩
 * ============================================================================
 *
 * 读取：扫描日志，取同一 key 中 sequence 最大的记录；最新记录是墓碑则视为不存在。
 *
 * 压缩（分区切换）：
 *   1) 扫描一遍活动区，建立「每个 key 的最新记录」槽位表；
 *   2) 擦除另一分区（其内容只会更旧）；
 *   3) 第二遍扫描，把每个 key 的最新、未删除记录按原序号复制过去；
 *   4) 最后写 SUPER 提交（代次 +1），随后擦除旧分区。
 *
 * 掉电安全性：
 *   - 步骤 3 中断：目标区没有 SUPER，不会被挂载选中，旧区仍完整；
 *   - 步骤 4 中断：旧区仍可挂载（代次更小）；
 *   - 步骤 4 完成后掉电：新区完整，旧区可丢弃。
 */

#include "fkv_internal.h"

/* 最新记录槽位表：压缩与遍历共用（调用者需保证不并发访问） */
static fkv_slot_t s_slots[FKV_MAX_KEYS];

/* ============================== 槽位表 ============================== */

static fkv_slot_t *slot_of(uint32_t key)
{
    uint32_t i;

    for (i = 0U; i < FKV_MAX_KEYS; i++) {
        if (s_slots[i].used != 0U && s_slots[i].key == key) {
            return &s_slots[i];
        }
    }
    return NULL;
}

typedef struct {
    uint32_t count;
} latest_ctx_t;

static int latest_cb(fkv_t *fs, const fkv_rec_hdr_t *hdr, uint32_t rec_addr,
                     void *ctx)
{
    latest_ctx_t *c = (latest_ctx_t *)ctx;
    fkv_slot_t *slot = slot_of(hdr->key);

    (void)fs;
    if (slot == NULL) {
        if (c->count >= FKV_MAX_KEYS) {
            return FKV_ERR_NOSPC;       /* 键数量超过槽位表容量 */
        }
        slot = &s_slots[c->count++];
        slot->used = 1U;
        slot->key = hdr->key;
        slot->seq = 0U;
    }
    if (slot->seq != 0U && (int32_t)(hdr->sequence - slot->seq) <= 0) {
        return FKV_OK;                  /* 已有更新记录 */
    }
    slot->seq = hdr->sequence;
    slot->addr = rec_addr;
    slot->len = hdr->length;
    slot->alive = ((hdr->flags & FKV_REC_FLAG_DELETED) == 0U) ? 1U : 0U;
    return FKV_OK;
}

int fkv_build_latest(fkv_t *fs, uint32_t *live_count)
{
    latest_ctx_t c;
    uint32_t i;
    uint32_t live = 0U;
    int rc;

    memset(s_slots, 0, sizeof(s_slots));
    c.count = 0U;

    rc = fkv_scan(fs, latest_cb, &c);
    if (rc != FKV_OK) {
        return rc;
    }
    for (i = 0U; i < c.count; i++) {
        if (s_slots[i].alive != 0U) {
            live++;
        }
    }
    if (live_count != NULL) {
        *live_count = live;
    }
    return FKV_OK;
}

/* ============================== 单键查找 ============================== */

typedef struct {
    uint32_t key;
    uint8_t found;
    fkv_rec_hdr_t hdr;
    uint32_t addr;
} key_ctx_t;

static int key_cb(fkv_t *fs, const fkv_rec_hdr_t *hdr, uint32_t rec_addr,
                  void *ctx)
{
    key_ctx_t *c = (key_ctx_t *)ctx;

    (void)fs;
    if (hdr->key != c->key) {
        return FKV_OK;
    }
    if (c->found != 0U && (int32_t)(hdr->sequence - c->hdr.sequence) <= 0) {
        return FKV_OK;
    }
    c->hdr = *hdr;
    c->addr = rec_addr;
    c->found = 1U;
    return FKV_OK;
}

/* 取某键最新记录；最新为墓碑时返回 FKV_ERR_NOENT */
static int key_find(fkv_t *fs, uint32_t key, key_ctx_t *out)
{
    int rc;

    memset(out, 0, sizeof(*out));
    out->key = key;
    rc = fkv_scan(fs, key_cb, out);
    if (rc != FKV_OK) {
        return rc;
    }
    if (out->found == 0U ||
        (out->hdr.flags & FKV_REC_FLAG_DELETED) != 0U) {
        return FKV_ERR_NOENT;
    }
    return FKV_OK;
}

/* ============================== 对外键值接口 ============================== */

int fkv_put(fkv_t *fs, uint32_t key, const void *value, uint32_t len)
{
    int rc;

    if (fs == NULL || !fs->mounted) {
        return FKV_ERR_STATE;
    }
    if (fs->readonly != 0U) {
        return FKV_ERR_READONLY;
    }
    if (len > fs->max_value) {
        return FKV_ERR_INVAL;
    }
    if (value == NULL && len != 0U) {
        return FKV_ERR_PARAM;
    }

    rc = fkv_append(fs, key, 0U, value, len);
    return rc;
}

int fkv_get(fkv_t *fs, uint32_t key, void *buf, uint32_t cap, uint32_t *len)
{
    key_ctx_t c;
    uint32_t n;
    int rc;

    if (fs == NULL || !fs->mounted || (buf == NULL && cap != 0U)) {
        return FKV_ERR_STATE;
    }

    rc = key_find(fs, key, &c);
    if (rc == FKV_OK) {
        n = (uint32_t)c.hdr.length;
        if (len != NULL) {
            *len = n;
        }
        if (buf != NULL) {
            if (cap < n) {
                rc = FKV_ERR_NOMEM;
            } else {
                rc = fkv_rec_read(fs, c.addr, &c.hdr, 0U, buf, n);
            }
        }
    }
    return rc;
}

int fkv_size(fkv_t *fs, uint32_t key, uint32_t *len)
{
    key_ctx_t c;
    int rc;

    if (fs == NULL || !fs->mounted || len == NULL) {
        return FKV_ERR_STATE;
    }

    rc = key_find(fs, key, &c);
    if (rc == FKV_OK) {
        *len = (uint32_t)c.hdr.length;
    }
    return rc;
}

int fkv_exists(fkv_t *fs, uint32_t key)
{
    key_ctx_t c;
    int rc;

    if (fs == NULL || !fs->mounted) {
        return FKV_ERR_STATE;
    }

    rc = key_find(fs, key, &c);
    return (rc == FKV_OK) ? 1 : ((rc == FKV_ERR_NOENT) ? 0 : rc);
}

int fkv_del(fkv_t *fs, uint32_t key)
{
    key_ctx_t c;
    int rc;

    if (fs == NULL || !fs->mounted) {
        return FKV_ERR_STATE;
    }
    if (fs->readonly != 0U) {
        return FKV_ERR_READONLY;
    }

    rc = key_find(fs, key, &c);
    if (rc == FKV_OK) {
        rc = fkv_append(fs, key, FKV_REC_FLAG_DELETED, NULL, 0U);
    }
    return rc;
}

int fkv_foreach(fkv_t *fs, fkv_visit_cb cb, void *ctx)
{
    uint32_t live = 0U;
    uint32_t i;
    int rc;

    if (fs == NULL || !fs->mounted || cb == NULL) {
        return FKV_ERR_STATE;
    }

    rc = fkv_build_latest(fs, &live);
    for (i = 0U; rc == FKV_OK && i < FKV_MAX_KEYS; i++) {
        const fkv_slot_t *slot = &s_slots[i];
        fkv_rec_hdr_t hdr;

        if (slot->used == 0U || slot->alive == 0U) {
            continue;
        }
        if (slot->len > fs->workbuf_size) {
            rc = FKV_ERR_NOMEM;
            break;
        }
        rc = fkv_hdr_read(fs, slot->addr, &hdr);
        if (rc != FKV_OK) {
            break;
        }
        if (slot->len > 0U) {
            rc = fkv_rec_read(fs, slot->addr, &hdr, 0U, fs->workbuf,
                               slot->len);
            if (rc != FKV_OK) {
                break;
            }
        }
        /* 注意：回调执行期间槽位表与工作缓冲仍在使用，回调内不要调用 fkv_* */
        rc = cb(ctx, slot->key,
                (slot->len > 0U) ? (const void *)fs->workbuf : NULL,
                slot->len);
        if (rc != FKV_OK) {
            break;
        }
    }
    return rc;
}

/* ============================== 分区滚动压缩 ============================== */

typedef struct {
    uint32_t target;
    uint32_t off;
    uint32_t copied;
    uint32_t live_bytes;
} copy_ctx_t;

static int copy_cb(fkv_t *fs, const fkv_rec_hdr_t *hdr, uint32_t rec_addr,
                   void *ctx)
{
    copy_ctx_t *c = (copy_ctx_t *)ctx;
    const fkv_slot_t *slot = slot_of(hdr->key);
    int rc;

    if (slot == NULL || slot->used == 0U || slot->alive == 0U ||
        slot->seq != hdr->sequence) {
        return FKV_OK;                  /* 非最新版本或已删除 */
    }
    if (hdr->length > fs->workbuf_size) {
        return FKV_ERR_NOMEM;
    }
    if (hdr->length > 0U) {
        rc = fkv_rec_read(fs, rec_addr, hdr, 0U, fs->workbuf, hdr->length);
        if (rc != FKV_OK) {
            return rc;
        }
    }

    rc = fkv_rec_write(fs, fkv_region_addr(fs, c->target), &c->off,
                        hdr->key, hdr->flags,
                        (hdr->length > 0U) ? (const void *)fs->workbuf : NULL,
                        hdr->length, hdr->sequence);
    if (rc != FKV_OK) {
        return rc;
    }
    c->copied++;
    c->live_bytes += hdr->length;
    return FKV_OK;
}

int fkv_compact(fkv_t *fs)
{
    uint32_t target;
    uint32_t live = 0U;
    copy_ctx_t c;
    fkv_super_t sb;
    int rc;

    if (!fs->mounted) {
        return FKV_ERR_STATE;
    }
    if (fs->readonly != 0U) {
        return FKV_ERR_READONLY;
    }

    target = (uint32_t)(fs->active ^ 1U);

    /* 1) 先确定每个键的最新记录；键过多则直接放弃，不破坏任何分区 */
    rc = fkv_build_latest(fs, &live);
    if (rc != FKV_OK) {
        return rc;
    }

    /* 2) 擦除目标分区（其内容只会比活动区更旧） */
    rc = fkv_dev_erase(fs, fkv_region_addr(fs, target), fs->region_size);
    if (rc != FKV_OK) {
        return rc;
    }

    /* 3) 按日志顺序复制每个键的最新、存活记录（沿用原序号） */
    memset(&c, 0, sizeof(c));
    c.target = target;
    c.off = FKV_SUPER_SIZE;
    rc = fkv_scan(fs, copy_cb, &c);
    if (rc != FKV_OK) {
        return rc;                      /* 目标区无 SUPER，掉电同样安全 */
    }

    /* 4) SUPER 最后写：此刻目标分区才算提交 */
    memset(&sb, 0, sizeof(sb));
    sb.generation = fs->generation + 1U;
    sb.record_count = c.copied;
    sb.live_bytes = c.live_bytes;
    rc = fkv_super_write(fs, target, &sb);
    if (rc != FKV_OK) {
        return rc;
    }

    /* 5) 切换内存状态 */
    fs->active = (uint8_t)target;
    fs->write_off = c.off;
    fs->generation = sb.generation;
    fs->record_count = c.copied;

    /* 6) 旧分区已无价值，擦除以便下一轮使用；失败不影响数据正确性 */
    (void)fkv_dev_erase(fs, fkv_region_addr(fs, fs->active ^ 1U),
                         fs->region_size);
    return FKV_OK;
}
