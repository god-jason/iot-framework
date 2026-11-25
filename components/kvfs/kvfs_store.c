/*
 * kvfs_store.c - 键值操作与分区滚动压缩
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

#include "kvfs_internal.h"

/* 最新记录槽位表：压缩与遍历共用，受实例锁保护 */
static kvfs_slot_t s_slots[KVFS_MAX_KEYS];

/* ============================== 槽位表 ============================== */

static kvfs_slot_t *slot_of(uint32_t key)
{
    uint32_t i;

    for (i = 0U; i < KVFS_MAX_KEYS; i++) {
        if (s_slots[i].used != 0U && s_slots[i].key == key) {
            return &s_slots[i];
        }
    }
    return NULL;
}

typedef struct {
    uint32_t count;
} latest_ctx_t;

static int latest_cb(kvfs_t *fs, const kvfs_rec_hdr_t *hdr, uint32_t rec_addr,
                     void *ctx)
{
    latest_ctx_t *c = (latest_ctx_t *)ctx;
    kvfs_slot_t *slot = slot_of(hdr->key);

    (void)fs;
    if (slot == NULL) {
        if (c->count >= KVFS_MAX_KEYS) {
            return KVFS_ERR_NOSPC;       /* 键数量超过槽位表容量 */
        }
        slot = &s_slots[c->count++];
        slot->used = 1U;
        slot->key = hdr->key;
        slot->seq = 0U;
    }
    if (slot->seq != 0U && (int32_t)(hdr->sequence - slot->seq) <= 0) {
        return KVFS_OK;                  /* 已有更新记录 */
    }
    slot->seq = hdr->sequence;
    slot->addr = rec_addr;
    slot->len = hdr->length;
    slot->alive = ((hdr->flags & KVFS_REC_FLAG_DELETED) == 0U) ? 1U : 0U;
    return KVFS_OK;
}

int kvfs_build_latest(kvfs_t *fs, uint32_t *live_count)
{
    latest_ctx_t c;
    uint32_t i;
    uint32_t live = 0U;
    int rc;

    memset(s_slots, 0, sizeof(s_slots));
    c.count = 0U;

    rc = kvfs_scan(fs, latest_cb, &c);
    if (rc != KVFS_OK) {
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
    return KVFS_OK;
}

/* ============================== 单键查找 ============================== */

typedef struct {
    uint32_t key;
    uint8_t found;
    kvfs_rec_hdr_t hdr;
    uint32_t addr;
} key_ctx_t;

static int key_cb(kvfs_t *fs, const kvfs_rec_hdr_t *hdr, uint32_t rec_addr,
                  void *ctx)
{
    key_ctx_t *c = (key_ctx_t *)ctx;

    (void)fs;
    if (hdr->key != c->key) {
        return KVFS_OK;
    }
    if (c->found != 0U && (int32_t)(hdr->sequence - c->hdr.sequence) <= 0) {
        return KVFS_OK;
    }
    c->hdr = *hdr;
    c->addr = rec_addr;
    c->found = 1U;
    return KVFS_OK;
}

/* 取某键最新记录；最新为墓碑时返回 KVFS_ERR_NOENT */
static int key_find(kvfs_t *fs, uint32_t key, key_ctx_t *out)
{
    int rc;

    memset(out, 0, sizeof(*out));
    out->key = key;
    rc = kvfs_scan(fs, key_cb, out);
    if (rc != KVFS_OK) {
        return rc;
    }
    if (out->found == 0U ||
        (out->hdr.flags & KVFS_REC_FLAG_DELETED) != 0U) {
        return KVFS_ERR_NOENT;
    }
    return KVFS_OK;
}

/* ============================== 对外键值接口 ============================== */

int kvfs_put(kvfs_t *fs, uint32_t key, const void *value, uint32_t len)
{
    int rc;

    if (fs == NULL || !fs->mounted) {
        return KVFS_ERR_STATE;
    }
    if (fs->readonly != 0U) {
        return KVFS_ERR_READONLY;
    }
    if (len > fs->max_value) {
        return KVFS_ERR_INVAL;
    }
    if (value == NULL && len != 0U) {
        return KVFS_ERR_PARAM;
    }

    kvfs_lock(fs);
    rc = kvfs_append(fs, key, 0U, value, len);
    kvfs_unlock(fs);
    return rc;
}

int kvfs_get(kvfs_t *fs, uint32_t key, void *buf, uint32_t cap, uint32_t *len)
{
    key_ctx_t c;
    uint32_t n;
    int rc;

    if (fs == NULL || !fs->mounted || (buf == NULL && cap != 0U)) {
        return KVFS_ERR_STATE;
    }

    kvfs_lock(fs);
    rc = key_find(fs, key, &c);
    if (rc == KVFS_OK) {
        n = (uint32_t)c.hdr.length;
        if (len != NULL) {
            *len = n;
        }
        if (buf != NULL) {
            if (cap < n) {
                rc = KVFS_ERR_NOMEM;
            } else {
                rc = kvfs_rec_read(fs, c.addr, &c.hdr, 0U, buf, n);
            }
        }
    }
    kvfs_unlock(fs);
    return rc;
}

int kvfs_size(kvfs_t *fs, uint32_t key, uint32_t *len)
{
    key_ctx_t c;
    int rc;

    if (fs == NULL || !fs->mounted || len == NULL) {
        return KVFS_ERR_STATE;
    }

    kvfs_lock(fs);
    rc = key_find(fs, key, &c);
    if (rc == KVFS_OK) {
        *len = (uint32_t)c.hdr.length;
    }
    kvfs_unlock(fs);
    return rc;
}

int kvfs_exists(kvfs_t *fs, uint32_t key)
{
    key_ctx_t c;
    int rc;

    if (fs == NULL || !fs->mounted) {
        return KVFS_ERR_STATE;
    }

    kvfs_lock(fs);
    rc = key_find(fs, key, &c);
    kvfs_unlock(fs);
    return (rc == KVFS_OK) ? 1 : ((rc == KVFS_ERR_NOENT) ? 0 : rc);
}

int kvfs_del(kvfs_t *fs, uint32_t key)
{
    key_ctx_t c;
    int rc;

    if (fs == NULL || !fs->mounted) {
        return KVFS_ERR_STATE;
    }
    if (fs->readonly != 0U) {
        return KVFS_ERR_READONLY;
    }

    kvfs_lock(fs);
    rc = key_find(fs, key, &c);
    if (rc == KVFS_OK) {
        rc = kvfs_append(fs, key, KVFS_REC_FLAG_DELETED, NULL, 0U);
    }
    kvfs_unlock(fs);
    return rc;
}

int kvfs_foreach(kvfs_t *fs, kvfs_visit_cb cb, void *ctx)
{
    uint32_t live = 0U;
    uint32_t i;
    int rc;

    if (fs == NULL || !fs->mounted || cb == NULL) {
        return KVFS_ERR_STATE;
    }

    kvfs_lock(fs);
    rc = kvfs_build_latest(fs, &live);
    for (i = 0U; rc == KVFS_OK && i < KVFS_MAX_KEYS; i++) {
        const kvfs_slot_t *slot = &s_slots[i];
        kvfs_rec_hdr_t hdr;

        if (slot->used == 0U || slot->alive == 0U) {
            continue;
        }
        if (slot->len > fs->workbuf_size) {
            rc = KVFS_ERR_NOMEM;
            break;
        }
        rc = kvfs_hdr_read(fs, slot->addr, &hdr);
        if (rc != KVFS_OK) {
            break;
        }
        if (slot->len > 0U) {
            rc = kvfs_rec_read(fs, slot->addr, &hdr, 0U, fs->workbuf,
                               slot->len);
            if (rc != KVFS_OK) {
                break;
            }
        }
        /* 注意：回调执行期间实例锁仍然持有，不要在回调里调用 kvfs_* */
        rc = cb(ctx, slot->key,
                (slot->len > 0U) ? (const void *)fs->workbuf : NULL,
                slot->len);
        if (rc != KVFS_OK) {
            break;
        }
    }
    kvfs_unlock(fs);
    return rc;
}

/* ============================== 分区滚动压缩 ============================== */

typedef struct {
    uint32_t target;
    uint32_t off;
    uint32_t copied;
    uint32_t live_bytes;
} copy_ctx_t;

static int copy_cb(kvfs_t *fs, const kvfs_rec_hdr_t *hdr, uint32_t rec_addr,
                   void *ctx)
{
    copy_ctx_t *c = (copy_ctx_t *)ctx;
    const kvfs_slot_t *slot = slot_of(hdr->key);
    int rc;

    if (slot == NULL || slot->used == 0U || slot->alive == 0U ||
        slot->seq != hdr->sequence) {
        return KVFS_OK;                  /* 非最新版本或已删除 */
    }
    if (hdr->length > fs->workbuf_size) {
        return KVFS_ERR_NOMEM;
    }
    if (hdr->length > 0U) {
        rc = kvfs_rec_read(fs, rec_addr, hdr, 0U, fs->workbuf, hdr->length);
        if (rc != KVFS_OK) {
            return rc;
        }
    }

    rc = kvfs_rec_write(fs, kvfs_region_addr(fs, c->target), &c->off,
                        hdr->key, hdr->flags,
                        (hdr->length > 0U) ? (const void *)fs->workbuf : NULL,
                        hdr->length, hdr->sequence);
    if (rc != KVFS_OK) {
        return rc;
    }
    c->copied++;
    c->live_bytes += hdr->length;
    return KVFS_OK;
}

int kvfs_compact(kvfs_t *fs)
{
    uint32_t target;
    uint32_t live = 0U;
    copy_ctx_t c;
    kvfs_super_t sb;
    int rc;

    if (!fs->mounted) {
        return KVFS_ERR_STATE;
    }
    if (fs->readonly != 0U) {
        return KVFS_ERR_READONLY;
    }

    target = (uint32_t)(fs->active ^ 1U);

    /* 1) 先确定每个键的最新记录；键过多则直接放弃，不破坏任何分区 */
    rc = kvfs_build_latest(fs, &live);
    if (rc != KVFS_OK) {
        return rc;
    }

    /* 2) 擦除目标分区（其内容只会比活动区更旧） */
    rc = kvfs_dev_erase(fs, kvfs_region_addr(fs, target), fs->region_size);
    if (rc != KVFS_OK) {
        return rc;
    }

    /* 3) 按日志顺序复制每个键的最新、存活记录（沿用原序号） */
    memset(&c, 0, sizeof(c));
    c.target = target;
    c.off = KVFS_SUPER_SIZE;
    rc = kvfs_scan(fs, copy_cb, &c);
    if (rc != KVFS_OK) {
        return rc;                      /* 目标区无 SUPER，掉电同样安全 */
    }

    /* 4) SUPER 最后写：此刻目标分区才算提交 */
    memset(&sb, 0, sizeof(sb));
    sb.generation = fs->generation + 1U;
    sb.record_count = c.copied;
    sb.live_bytes = c.live_bytes;
    rc = kvfs_super_write(fs, target, &sb);
    if (rc != KVFS_OK) {
        return rc;
    }

    /* 5) 切换内存状态 */
    fs->active = (uint8_t)target;
    fs->write_off = c.off;
    fs->generation = sb.generation;
    fs->record_count = c.copied;

    /* 6) 旧分区已无价值，擦除以便下一轮使用；失败不影响数据正确性 */
    (void)kvfs_dev_erase(fs, kvfs_region_addr(fs, fs->active ^ 1U),
                         fs->region_size);
    return KVFS_OK;
}
