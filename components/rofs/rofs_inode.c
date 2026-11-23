/*
 * rofs_inode.c - Inode 记录、最新版本查找与目录项查询
 * ============================================================================
 *
 * Inode Payload 格式（小端，固定前缀 32B + 名称）：
 *
 *   +0   inode        4B
 *   +4   parent       4B
 *   +8   size         4B
 *   +12  version      4B
 *   +16  create_time  4B
 *   +20  modify_time  4B
 *   +24  epoch        4B
 *   +28  name_len     2B
 *   +30  type         1B
 *   +31  flags        1B
 *   +32  name         name_len 字节
 *
 * 版本规则：
 *   - 同一 Inode 的每次更新都写入新记录，version 单调递增；
 *   - 查找时取 version 最大者；version 相同时取 sequence 更大者；
 *   - INODE_DELETE 记录只包含 inode + version，删除后该 Inode 视为不存在。
 *
 * 目录查找：
 *   V1 不在 RAM 中维护目录树，也不缓存 Inode。
 *   “父目录 + 名称”通过扫描全部日志中的 INODE 记录来定位，
 *   属于按需查询，RAM 恒定但时间复杂度为 O(N)。
 */

#include "rofs_internal.h"

/* INODE Payload 最大长度：固定前缀 + 最长名称 */
#define ROFS_INODE_BUF   (ROFS_INODE_PREFIX + ROFS_NAME_MAX)

/* 读取 config.get_time；未提供时返回 0 */
uint32_t rofs__now(rofs_t *fs)
{
    if (fs != NULL && fs->cfg.get_time != NULL) {
        return fs->cfg.get_time();
    }
    return 0u;
}

/*
 * 将 Inode 信息序列化为 INODE Payload。
 * 返回实际 Payload 长度（32 + name_len）。
 */
static uint32_t inode_serialize(const rofs_ii_t *info, uint8_t *buf)
{
    uint32_t len = ROFS_INODE_PREFIX + info->name_len;

    rofs__put_u32(&buf[0], info->inode);
    rofs__put_u32(&buf[4], info->parent);
    rofs__put_u32(&buf[8], info->size);
    rofs__put_u32(&buf[12], info->version);
    rofs__put_u32(&buf[16], info->create_time);
    rofs__put_u32(&buf[20], info->modify_time);
    rofs__put_u32(&buf[24], info->epoch);
    rofs__put_u16(&buf[28], info->name_len);
    buf[30] = info->type;
    buf[31] = info->flags;
    if (info->name_len > 0u) {
        memcpy(&buf[ROFS_INODE_PREFIX], info->name, info->name_len);
    }
    return len;
}

/*
 * 解析 INODE Payload。
 * 会校验长度与 name_len，异常数据返回 ROFS_ERR_CORRUPT。
 */
static int inode_parse(const uint8_t *buf, uint32_t len, rofs_ii_t *info)
{
    uint16_t name_len;

    if (len < ROFS_INODE_PREFIX) {
        return ROFS_ERR_CORRUPT;
    }

    memset(info, 0, sizeof(*info));
    info->inode = rofs__get_u32(&buf[0]);
    info->parent = rofs__get_u32(&buf[4]);
    info->size = rofs__get_u32(&buf[8]);
    info->version = rofs__get_u32(&buf[12]);
    info->create_time = rofs__get_u32(&buf[16]);
    info->modify_time = rofs__get_u32(&buf[20]);
    info->epoch = rofs__get_u32(&buf[24]);
    name_len = rofs__get_u16(&buf[28]);
    info->type = buf[30];
    info->flags = buf[31];

    if (name_len > ROFS_NAME_MAX || len < ROFS_INODE_PREFIX + name_len) {
        return ROFS_ERR_CORRUPT;
    }
    info->name_len = name_len;
    if (name_len > 0u) {
        memcpy(info->name, &buf[ROFS_INODE_PREFIX], name_len);
    }
    info->name[name_len] = '\0';
    return ROFS_OK;
}

/*
 * 按指定记录类型写入一条 Inode 记录。
 * CREATE / UPDATE 使用完整 Payload；
 * DELETE 由 rofs__inode_delete 单独构造更短的 Payload。
 */
int rofs__inode_write(rofs_t *fs, uint16_t rec_type, const rofs_ii_t *info)
{
    uint8_t buf[ROFS_INODE_BUF];
    uint32_t len;

    if (info->name_len > ROFS_NAME_MAX) {
        return ROFS_ERR_PARAM;
    }
    len = inode_serialize(info, buf);
    return rofs__rec_write(fs, rec_type, info->inode, buf, len, NULL, 0u);
}

/* ---------------------------------------------------------------------------
 * 最新 Inode 查找
 * -------------------------------------------------------------------------*/

typedef struct {
    rofs_inode_t inode;   /* 目标 Inode */
    uint8_t found;        /* 是否找到 */
    uint16_t rec_type;    /* 最新记录类型（区分 DELETE） */
    uint32_t version;     /* 当前最大版本 */
    uint32_t seq;         /* 当前最大版本对应的 sequence */
    uint32_t addr;        /* 最新记录地址，用于最后读取完整 Payload */
} inode_find_ctx_t;

/*
 * 扫描回调：只关心匹配 inode 的 INODE_* 记录。
 * 为了比较版本，仅读取 Payload 的前 16B（UPDATE/CREATE）
 * 或前 8B（DELETE），不搬运完整名称。
 */
static int inode_find_cb(rofs_t *fs, const rofs_rec_hdr_t *hdr,
                         uint32_t rec_addr, void *ctx)
{
    inode_find_ctx_t *f = (inode_find_ctx_t *)ctx;
    uint8_t buf[ROFS_INODE_BUF];
    uint32_t len;
    uint32_t version;
    int err;

    if (hdr->object_id != f->inode) {
        return ROFS_OK;
    }
    if (hdr->type != ROFS_REC_INODE_CREATE &&
        hdr->type != ROFS_REC_INODE_UPDATE &&
        hdr->type != ROFS_REC_INODE_DELETE) {
        return ROFS_OK;
    }

    len = hdr->payload_size;
    if (len > sizeof(buf)) {
        return ROFS_OK;
    }
    if (len < 8u) {
        return ROFS_OK;
    }
    err = rofs__rec_read_raw(fs, rec_addr, hdr, 0u, buf, len);
    if (err != ROFS_OK) {
        return err;
    }

    if (hdr->type == ROFS_REC_INODE_DELETE) {
        /* DELETE Payload: inode(4B) + version(4B) */
        version = rofs__get_u32(&buf[4]);
    } else {
        if (len < ROFS_INODE_PREFIX) {
            return ROFS_OK;
        }
        version = rofs__get_u32(&buf[12]);
    }

    /* 版本大者优先；版本相同则 sequence 大者优先 */
    if (!f->found || version > f->version ||
        (version == f->version && hdr->sequence > f->seq)) {
        f->found = 1u;
        f->rec_type = hdr->type;
        f->version = version;
        f->seq = hdr->sequence;
        f->addr = rec_addr;
    }
    return ROFS_OK;
}

/*
 * 查找 Inode 的最新有效版本。
 * 流程：全盘扫描收集最优记录 -> 读取其完整 Payload -> 解析。
 */
int rofs__inode_find(rofs_t *fs, rofs_inode_t inode, rofs_ii_t *info)
{
    inode_find_ctx_t f;
    rofs_rec_hdr_t hdr;
    uint8_t buf[ROFS_INODE_BUF];
    uint32_t next;
    int err;

    if (fs == NULL || info == NULL || inode == ROFS_INVALID_INODE) {
        return ROFS_ERR_PARAM;
    }

    memset(&f, 0, sizeof(f));
    f.inode = inode;

    err = rofs__scan_all(fs, inode_find_cb, &f);
    if (err != ROFS_OK) {
        return err;
    }
    if (!f.found || f.rec_type == ROFS_REC_INODE_DELETE) {
        return ROFS_ERR_NOENT;
    }

    err = rofs__rec_read(fs, f.addr, &hdr, buf, sizeof(buf), &next);
    if (err != ROFS_OK) {
        return err;
    }
    err = inode_parse(buf, hdr.payload_size, info);
    if (err != ROFS_OK) {
        return err;
    }
    info->rec_seq = hdr.sequence;
    return ROFS_OK;
}

/* ---------------------------------------------------------------------------
 * 目录项查询
 * -------------------------------------------------------------------------*/

typedef struct {
    rofs_inode_t parent;   /* 目标父目录 */
    const char *name;      /* 目标名称 */
    uint32_t cand[8];      /* 候选 Inode 编号（同一路径可能有多条历史记录） */
    uint8_t count;
} inode_lookup_ctx_t;

/*
 * 扫描回调：收集所有“父目录 + 名称”匹配的 Inode 编号。
 * 历史记录可能对应已改名的旧状态，因此这里只收集候选，
 * 最终由 rofs__inode_lookup 逐个确认其最新版本是否仍匹配。
 */
static int inode_lookup_cb(rofs_t *fs, const rofs_rec_hdr_t *hdr,
                           uint32_t rec_addr, void *ctx)
{
    inode_lookup_ctx_t *l = (inode_lookup_ctx_t *)ctx;
    uint8_t buf[ROFS_INODE_BUF];
    rofs_ii_t info;
    uint32_t len;
    uint32_t i;
    int err;

    if (hdr->type != ROFS_REC_INODE_CREATE &&
        hdr->type != ROFS_REC_INODE_UPDATE) {
        return ROFS_OK;
    }
    len = hdr->payload_size;
    if (len > sizeof(buf)) {
        return ROFS_OK;
    }
    err = rofs__rec_read_raw(fs, rec_addr, hdr, 0u, buf, len);
    if (err != ROFS_OK) {
        return err;
    }
    err = inode_parse(buf, len, &info);
    if (err != ROFS_OK) {
        return ROFS_OK;
    }
    if (info.parent != l->parent) {
        return ROFS_OK;
    }
    if (strlen(l->name) != info.name_len ||
        memcmp(info.name, l->name, info.name_len) != 0) {
        return ROFS_OK;
    }

    /* 去重后加入候选表；候选表满时忽略后续项 */
    for (i = 0; i < l->count; i++) {
        if (l->cand[i] == info.inode) {
            return ROFS_OK;
        }
    }
    if (l->count < (uint8_t)(sizeof(l->cand) / sizeof(l->cand[0]))) {
        l->cand[l->count++] = info.inode;
    }
    return ROFS_OK;
}

/*
 * 在 parent 目录下按名称查找子项。
 * 返回该子项最新 Inode 信息；名称已被改名或已删除时返回 NOENT。
 */
int rofs__inode_lookup(rofs_t *fs, rofs_inode_t parent, const char *name,
                       rofs_ii_t *info)
{
    inode_lookup_ctx_t l;
    rofs_ii_t tmp;
    uint32_t i;
    int err;

    if (fs == NULL || name == NULL || info == NULL) {
        return ROFS_ERR_PARAM;
    }
    if (strlen(name) == 0u || strlen(name) > ROFS_NAME_MAX) {
        return ROFS_ERR_PARAM;
    }

    memset(&l, 0, sizeof(l));
    l.parent = parent;
    l.name = name;

    err = rofs__scan_all(fs, inode_lookup_cb, &l);
    if (err != ROFS_OK) {
        return err;
    }

    for (i = 0; i < l.count; i++) {
        err = rofs__inode_find(fs, l.cand[i], &tmp);
        if (err != ROFS_OK) {
            continue;
        }
        if (tmp.parent != parent) {
            continue;
        }
        if (strlen(name) != tmp.name_len ||
            memcmp(tmp.name, name, tmp.name_len) != 0) {
            continue;
        }
        *info = tmp;
        return ROFS_OK;
    }

    return ROFS_ERR_NOENT;
}

/* ---------------------------------------------------------------------------
 * Inode 创建 / 更新 / 删除
 * -------------------------------------------------------------------------*/

/*
 * 创建 Inode。
 * parent 必须存在且为目录；parent == ROFS_INVALID_INODE 用于格式化时创建根。
 * 新 Inode 编号来自 fs->next_inode，单调递增，不复用。
 */
int rofs__inode_create(rofs_t *fs, rofs_inode_t parent, const char *name,
                       uint8_t type, rofs_ii_t *info)
{
    rofs_ii_t parent_info;
    int err;

    if (fs == NULL || name == NULL || info == NULL) {
        return ROFS_ERR_PARAM;
    }
    if (strlen(name) > ROFS_NAME_MAX) {
        return ROFS_ERR_PARAM;
    }
    if (fs->readonly) {
        return ROFS_ERR_READONLY;
    }
    if (parent != ROFS_INVALID_INODE) {
        err = rofs__inode_find(fs, parent, &parent_info);
        if (err != ROFS_OK) {
            return err;
        }
        if (parent_info.type != ROFS_TYPE_DIR) {
            return ROFS_ERR_NOTDIR;
        }
    }

    memset(info, 0, sizeof(*info));
    info->inode = fs->next_inode++;
    info->parent = parent;
    info->size = 0u;
    info->version = 1u;
    info->create_time = rofs__now(fs);
    info->modify_time = info->create_time;
    info->epoch = 1u;
    info->name_len = (uint16_t)strlen(name);
    info->type = type;
    if (info->name_len > 0u) {
        memcpy(info->name, name, info->name_len);
    }
    info->name[info->name_len] = '\0';

    err = rofs__inode_write(fs, ROFS_REC_INODE_CREATE, info);
    if (err != ROFS_OK) {
        return err;
    }
    info->rec_seq = fs->seq - 1u;
    return ROFS_OK;
}

/*
 * 更新 Inode：版本号 +1，刷新修改时间，写入 INODE_UPDATE。
 * 写入失败时回滚内存中的版本号，保持调用者视图一致。
 */
int rofs__inode_update(rofs_t *fs, rofs_ii_t *info)
{
    int err;

    if (fs == NULL || info == NULL) {
        return ROFS_ERR_PARAM;
    }
    if (fs->readonly) {
        return ROFS_ERR_READONLY;
    }

    info->version++;
    info->modify_time = rofs__now(fs);

    err = rofs__inode_write(fs, ROFS_REC_INODE_UPDATE, info);
    if (err != ROFS_OK) {
        info->version--;
        return err;
    }
    info->rec_seq = fs->seq - 1u;
    return ROFS_OK;
}

/*
 * 删除 Inode：写入 INODE_DELETE。
 * DELETE 记录使用 version + 1，确保删除状态一定是最新版本。
 * 物理空间不回收，留待后续 GC。
 */
int rofs__inode_delete(rofs_t *fs, const rofs_ii_t *info)
{
    uint8_t buf[8];

    if (fs == NULL || info == NULL) {
        return ROFS_ERR_PARAM;
    }
    if (fs->readonly) {
        return ROFS_ERR_READONLY;
    }

    rofs__put_u32(&buf[0], info->inode);
    rofs__put_u32(&buf[4], info->version + 1u);
    return rofs__rec_write(fs, ROFS_REC_INODE_DELETE, info->inode,
                           buf, sizeof(buf), NULL, 0u);
}
