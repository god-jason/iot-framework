/*
 * rofs_dir.c - 路径解析、目录管理与目录遍历
 * ============================================================================
 *
 * 路径规则（V1）：
 *   - 必须以 '/' 开头；
 *   - 长度不超过 ROFS_PATH_MAX；
 *   - 不允许空分量（"//"）、"."、".."；
 *   - 除根 "/" 外不允许以 '/' 结尾；
 *   - 单个分量不超过 ROFS_NAME_MAX。
 *
 * 目录模型：
 *   - 目录也是 Inode，子项通过 (parent, name) 关联；
 *   - 不维护常驻目录树，遍历时扫描日志中的 INODE 记录；
 *   - 删除/改名都通过追加 INODE 记录完成，旧记录由 GC 后续回收。
 */

#include "rofs_internal.h"

/* ============================================================================
 * 路径工具
 * ==========================================================================*/

/*
 * 路径合法性检查。
 * 返回 ROFS_OK 或 ROFS_ERR_INVAL。
 */
static int path_validate(const char *path)
{
    uint32_t len;
    uint32_t i;
    uint32_t start;

    if (path == NULL || path[0] != '/') {
        return ROFS_ERR_INVAL;
    }
    len = (uint32_t)strlen(path);
    if (len > ROFS_PATH_MAX) {
        return ROFS_ERR_INVAL;
    }
    if (len == 1u) {
        return ROFS_OK;                  /* 根目录 */
    }
    if (path[len - 1u] == '/') {
        return ROFS_ERR_INVAL;           /* 禁止结尾 '/' */
    }

    /* 逐段检查长度以及 "." / ".." */
    i = 1u;
    while (i < len) {
        start = i;
        while (i < len && path[i] != '/') {
            i++;
        }
        if (i == start || i - start > ROFS_NAME_MAX) {
            return ROFS_ERR_INVAL;
        }
        if ((i - start == 1u && path[start] == '.') ||
            (i - start == 2u && path[start] == '.' &&
             path[start + 1u] == '.')) {
            return ROFS_ERR_INVAL;
        }
        i++;                             /* 跳过 '/' */
    }
    return ROFS_OK;
}

/*
 * 从 path 的 *idx 位置取出下一个路径分量。
 * 成功返回分量长度，并写入 name（保证 '\0' 结尾）。
 * *idx 会推进到下一个分量的起始位置。
 */
static uint32_t path_component(const char *path, uint32_t *idx,
                               char *name, uint32_t cap)
{
    uint32_t len = (uint32_t)strlen(path);
    uint32_t start = *idx;
    uint32_t n;

    while (*idx < len && path[*idx] != '/') {
        (*idx)++;
    }
    n = *idx - start;
    if (n >= cap) {
        n = cap - 1u;
    }
    memcpy(name, &path[start], n);
    name[n] = '\0';
    if (*idx < len) {
        (*idx)++;                        /* 跳过 '/' */
    }
    return n;
}

/*
 * 解析完整路径，返回目标 Inode 信息。
 * "/" 直接返回根目录；中间分量必须是目录。
 */
int rofs__path_lookup(rofs_t *fs, const char *path, rofs_ii_t *info)
{
    rofs_ii_t cur;
    char name[ROFS_NAME_MAX + 1];
    uint32_t idx;
    int err;

    err = path_validate(path);
    if (err != ROFS_OK) {
        return err;
    }

    err = rofs__inode_find(fs, ROFS_ROOT_INODE, &cur);
    if (err != ROFS_OK) {
        return err;
    }
    if (strlen(path) == 1u) {
        *info = cur;
        return ROFS_OK;
    }

    idx = 1u;
    while (idx < (uint32_t)strlen(path)) {
        path_component(path, &idx, name, sizeof(name));
        err = rofs__inode_lookup(fs, cur.inode, name, &cur);
        if (err != ROFS_OK) {
            return err;
        }
        /* 后面还有分量时，当前目标必须是目录 */
        if (idx < (uint32_t)strlen(path) && cur.type != ROFS_TYPE_DIR) {
            return ROFS_ERR_NOTDIR;
        }
    }

    *info = cur;
    return ROFS_OK;
}

/*
 * 解析父目录与最后一段名称。
 * 用于 mkdir/open/rename 等需要“在某个目录下创建/查找子项”的操作。
 * 路径为 "/" 时返回 ROFS_ERR_INVAL。
 */
int rofs__path_parent(rofs_t *fs, const char *path, rofs_ii_t *parent,
                      char *name)
{
    rofs_ii_t cur;
    char comp[ROFS_NAME_MAX + 1];
    uint32_t idx;
    uint32_t len;
    int err;

    err = path_validate(path);
    if (err != ROFS_OK) {
        return err;
    }
    len = (uint32_t)strlen(path);
    if (len == 1u) {
        return ROFS_ERR_INVAL;
    }

    err = rofs__inode_find(fs, ROFS_ROOT_INODE, &cur);
    if (err != ROFS_OK) {
        return err;
    }

    idx = 1u;
    while (idx < len) {
        path_component(path, &idx, comp, sizeof(comp));
        if (idx >= len) {
            /* 最后一段：当前 cur 就是父目录 */
            strcpy(name, comp);
            *parent = cur;
            return ROFS_OK;
        }
        /* 中间分量：必须存在且是目录 */
        err = rofs__inode_lookup(fs, cur.inode, comp, &cur);
        if (err != ROFS_OK) {
            return err;
        }
        if (cur.type != ROFS_TYPE_DIR) {
            return ROFS_ERR_NOTDIR;
        }
    }

    return ROFS_ERR_INVAL;
}

/* ============================================================================
 * 目录管理
 * ==========================================================================*/

/*
 * 创建目录。
 * 父目录必须存在；同名项已存在返回 ROFS_ERR_EXIST。
 */
int rofs_mkdir(rofs_t *fs, const char *path)
{
    rofs_ii_t parent;
    rofs_ii_t info;
    char name[ROFS_NAME_MAX + 1];
    int err;

    if (fs == NULL || !fs->mounted) {
        return ROFS_ERR_STATE;
    }

    rofs__lock(fs);
    err = rofs__path_parent(fs, path, &parent, name);
    if (err == ROFS_OK) {
        err = rofs__inode_lookup(fs, parent.inode, name, &info);
        if (err == ROFS_OK) {
            err = ROFS_ERR_EXIST;
        } else if (err == ROFS_ERR_NOENT) {
            err = rofs__inode_create(fs, parent.inode, name, ROFS_TYPE_DIR,
                                     &info);
        }
    }
    rofs__unlock(fs);
    return err;
}

/*
 * 删除文件。
 * 目标是目录时返回 ROFS_ERR_ISDIR；根目录不可删除。
 */
int rofs_unlink(rofs_t *fs, const char *path)
{
    rofs_ii_t info;
    int err;

    if (fs == NULL || !fs->mounted) {
        return ROFS_ERR_STATE;
    }

    rofs__lock(fs);
    err = rofs__path_lookup(fs, path, &info);
    if (err == ROFS_OK) {
        if (info.type == ROFS_TYPE_DIR) {
            err = ROFS_ERR_ISDIR;
        } else if (info.inode == ROFS_ROOT_INODE) {
            err = ROFS_ERR_INVAL;
        } else {
            err = rofs__inode_delete(fs, &info);
        }
    }
    rofs__unlock(fs);
    return err;
}

/* 收集某个目录下的候选子 Inode 编号 */
typedef struct {
    rofs_inode_t parent;
    uint32_t cand[8];
    uint8_t count;
} dir_child_ctx_t;

/*
 * 扫描回调：收集 parent 目录下出现过的子 Inode。
 * 只读取 INODE Payload 前缀，不解析名称。
 */
static int dir_child_cb(rofs_t *fs, const rofs_rec_hdr_t *hdr,
                        uint32_t rec_addr, void *ctx)
{
    dir_child_ctx_t *c = (dir_child_ctx_t *)ctx;
    uint8_t buf[ROFS_INODE_PREFIX + ROFS_NAME_MAX];
    rofs_ii_t info;
    uint32_t len;
    uint32_t i;
    int err;

    if (hdr->type != ROFS_REC_INODE_CREATE &&
        hdr->type != ROFS_REC_INODE_UPDATE) {
        return ROFS_OK;
    }
    len = hdr->payload_size;
    if (len > sizeof(buf) || len < ROFS_INODE_PREFIX) {
        return ROFS_OK;
    }
    err = rofs__rec_read_raw(fs, rec_addr, hdr, 0u, buf, len);
    if (err != ROFS_OK) {
        return err;
    }
    memset(&info, 0, sizeof(info));
    info.inode = rofs__get_u32(&buf[0]);
    info.parent = rofs__get_u32(&buf[4]);
    if (info.parent != c->parent) {
        return ROFS_OK;
    }
    /* 去重，最多记录 8 个候选 */
    for (i = 0; i < c->count; i++) {
        if (c->cand[i] == info.inode) {
            return ROFS_OK;
        }
    }
    if (c->count < (uint8_t)(sizeof(c->cand) / sizeof(c->cand[0]))) {
        c->cand[c->count++] = info.inode;
    }
    return ROFS_OK;
}

/*
 * 判断目录是否为空。
 * 由于历史记录可能已被删除/改名，需要逐个候选检查其最新版本。
 * 返回 ROFS_OK 表示空，ROFS_ERR_NOTEMPTY 表示非空。
 */
static int dir_is_empty(rofs_t *fs, rofs_inode_t inode)
{
    dir_child_ctx_t c;
    rofs_ii_t info;
    uint32_t i;
    int err;

    memset(&c, 0, sizeof(c));
    c.parent = inode;

    err = rofs__scan_all(fs, dir_child_cb, &c);
    if (err != ROFS_OK) {
        return err;
    }

    for (i = 0; i < c.count; i++) {
        err = rofs__inode_find(fs, c.cand[i], &info);
        if (err == ROFS_OK && info.parent == inode) {
            return ROFS_ERR_NOTEMPTY;
        }
    }
    return ROFS_OK;
}

/*
 * 删除空目录。
 * 根目录不可删除；非空目录返回 ROFS_ERR_NOTEMPTY。
 */
int rofs_rmdir(rofs_t *fs, const char *path)
{
    rofs_ii_t info;
    int err;

    if (fs == NULL || !fs->mounted) {
        return ROFS_ERR_STATE;
    }

    rofs__lock(fs);
    err = rofs__path_lookup(fs, path, &info);
    if (err == ROFS_OK) {
        if (info.type != ROFS_TYPE_DIR) {
            err = ROFS_ERR_NOTDIR;
        } else if (info.inode == ROFS_ROOT_INODE) {
            err = ROFS_ERR_INVAL;
        } else {
            err = dir_is_empty(fs, info.inode);
            if (err == ROFS_OK) {
                err = rofs__inode_delete(fs, &info);
            }
        }
    }
    rofs__unlock(fs);
    return err;
}

/*
 * 重命名/移动。
 *
 * V1 语义：
 *   - 目标已存在：同 Inode 视为成功，否则返回 ROFS_ERR_EXIST；
 *   - 目标父目录必须存在；
 *   - 目录不能移动到自己或自己的子树下（防止形成环）。
 *
 * 原子性：只写一条 INODE_UPDATE 记录，失败不会留下中间状态。
 */
int rofs_rename(rofs_t *fs, const char *oldpath, const char *newpath)
{
    rofs_ii_t old_info;
    rofs_ii_t new_parent;
    rofs_ii_t tmp;
    rofs_ii_t info;
    char name[ROFS_NAME_MAX + 1];
    uint32_t guard = 0u;
    int err;

    if (fs == NULL || !fs->mounted) {
        return ROFS_ERR_STATE;
    }

    rofs__lock(fs);

    err = rofs__path_lookup(fs, oldpath, &old_info);
    if (err != ROFS_OK) {
        rofs__unlock(fs);
        return err;
    }
    if (old_info.inode == ROFS_ROOT_INODE) {
        rofs__unlock(fs);
        return ROFS_ERR_INVAL;
    }

    err = rofs__path_parent(fs, newpath, &new_parent, name);
    if (err != ROFS_OK) {
        rofs__unlock(fs);
        return err;
    }

    err = rofs__inode_lookup(fs, new_parent.inode, name, &info);
    if (err == ROFS_OK) {
        err = (info.inode == old_info.inode) ? ROFS_OK : ROFS_ERR_EXIST;
        rofs__unlock(fs);
        return err;
    }
    if (err != ROFS_ERR_NOENT) {
        rofs__unlock(fs);
        return err;
    }

    /* 目录不能移动到自身子树内：沿新父目录向上找祖先 */
    if (old_info.type == ROFS_TYPE_DIR) {
        tmp = new_parent;
        while (tmp.inode != ROFS_ROOT_INODE && guard < 64u) {
            if (tmp.inode == old_info.inode) {
                rofs__unlock(fs);
                return ROFS_ERR_INVAL;
            }
            err = rofs__inode_find(fs, tmp.parent, &tmp);
            if (err != ROFS_OK) {
                break;
            }
            guard++;
        }
    }

    /* 单条 INODE_UPDATE 完成名称与父目录变更 */
    old_info.parent = new_parent.inode;
    old_info.name_len = (uint16_t)strlen(name);
    memcpy(old_info.name, name, old_info.name_len);
    old_info.name[old_info.name_len] = '\0';

    err = rofs__inode_update(fs, &old_info);
    rofs__unlock(fs);
    return err;
}

/* 查询属性；路径不存在返回 ROFS_ERR_NOENT */
int rofs_stat(rofs_t *fs, const char *path, rofs_stat_t *stat)
{
    rofs_ii_t info;
    int err;

    if (fs == NULL || stat == NULL || !fs->mounted) {
        return ROFS_ERR_STATE;
    }

    rofs__lock(fs);
    err = rofs__path_lookup(fs, path, &info);
    if (err == ROFS_OK) {
        stat->inode = info.inode;
        stat->parent = info.parent;
        stat->size = info.size;
        stat->version = info.version;
        stat->create_time = info.create_time;
        stat->modify_time = info.modify_time;
        stat->type = info.type;
        memcpy(stat->name, info.name, info.name_len + 1u);
    }
    rofs__unlock(fs);
    return err;
}

/* 存在返回 1，不存在返回 0，其他为负错误码 */
int rofs_exists(rofs_t *fs, const char *path)
{
    rofs_ii_t info;
    int err;

    if (fs == NULL || !fs->mounted) {
        return ROFS_ERR_STATE;
    }

    rofs__lock(fs);
    err = rofs__path_lookup(fs, path, &info);
    rofs__unlock(fs);

    if (err == ROFS_OK) {
        return 1;
    }
    return (err == ROFS_ERR_NOENT) ? 0 : err;
}

/* 是目录返回 1，否则返回 0，其他为负错误码 */
int rofs_isdir(rofs_t *fs, const char *path)
{
    rofs_ii_t info;
    int err;

    if (fs == NULL || !fs->mounted) {
        return ROFS_ERR_STATE;
    }

    rofs__lock(fs);
    err = rofs__path_lookup(fs, path, &info);
    rofs__unlock(fs);

    if (err == ROFS_OK) {
        return (info.type == ROFS_TYPE_DIR) ? 1 : 0;
    }
    return (err == ROFS_ERR_NOENT) ? 0 : err;
}

/* ============================================================================
 * 目录遍历
 * ==========================================================================*/

/* 打开目录，返回内部句柄池中的指针 */
int rofs_opendir(rofs_t *fs, const char *path, rofs_dir_t **dir)
{
    rofs_ii_t info;
    rofs_dir_t *slot = NULL;
    uint32_t i;
    int err;

    if (fs == NULL || path == NULL || dir == NULL || !fs->mounted) {
        return ROFS_ERR_STATE;
    }

    rofs__lock(fs);
    err = rofs__path_lookup(fs, path, &info);
    if (err != ROFS_OK) {
        rofs__unlock(fs);
        return err;
    }
    if (info.type != ROFS_TYPE_DIR) {
        rofs__unlock(fs);
        return ROFS_ERR_NOTDIR;
    }

    for (i = 0; i < fs->cfg.max_open_dirs; i++) {
        if (fs->dirs[i].used == 0u) {
            slot = &fs->dirs[i];
            break;
        }
    }
    if (slot == NULL) {
        rofs__unlock(fs);
        return ROFS_ERR_NOMEM;
    }

    slot->fs = fs;
    slot->inode = info.inode;
    slot->iter_seq = 0u;
    slot->used = 1u;
    *dir = slot;
    rofs__unlock(fs);
    return ROFS_OK;
}

/*
 * readdir 扫描上下文。
 * 目标：在所有 parent 匹配的 INODE 记录中，找出 sequence 最小且
 * 大于 iter_seq、同时其 Inode 最新版本仍属于该目录的一项。
 */
typedef struct {
    rofs_dir_t *dir;
    uint8_t found;
    uint32_t best_seq;
    rofs_ii_t best;
} dir_read_ctx_t;

/*
 * readdir 扫描回调。
 * 由于日志中同一子项可能有多条历史记录，需要：
 *   1. 只考虑 sequence > iter_seq；
 *   2. 解析记录中的 (inode, parent, name)；
 *   3. 用 rofs__inode_find 找到该 Inode 的最新版本；
 *   4. 最新版本仍匹配 parent+name 才作为候选；
 *   5. 在候选中选 sequence 最小者，实现稳定的逐条遍历。
 */
static int dir_read_cb(rofs_t *fs, const rofs_rec_hdr_t *hdr,
                       uint32_t rec_addr, void *ctx)
{
    dir_read_ctx_t *r = (dir_read_ctx_t *)ctx;
    uint8_t buf[ROFS_INODE_PREFIX + ROFS_NAME_MAX];
    rofs_ii_t cand;
    rofs_ii_t latest;
    uint32_t len;
    int err;

    if (hdr->type != ROFS_REC_INODE_CREATE &&
        hdr->type != ROFS_REC_INODE_UPDATE) {
        return ROFS_OK;
    }
    if (hdr->sequence <= r->dir->iter_seq) {
        return ROFS_OK;
    }
    len = hdr->payload_size;
    if (len > sizeof(buf) || len < ROFS_INODE_PREFIX) {
        return ROFS_OK;
    }
    err = rofs__rec_read_raw(fs, rec_addr, hdr, 0u, buf, len);
    if (err != ROFS_OK) {
        return err;
    }
    memset(&cand, 0, sizeof(cand));
    cand.inode = rofs__get_u32(&buf[0]);
    cand.parent = rofs__get_u32(&buf[4]);
    cand.name_len = rofs__get_u16(&buf[28]);
    if (cand.parent != r->dir->inode ||
        cand.name_len > ROFS_NAME_MAX ||
        len < ROFS_INODE_PREFIX + cand.name_len) {
        return ROFS_OK;
    }
    memcpy(cand.name, &buf[ROFS_INODE_PREFIX], cand.name_len);
    cand.name[cand.name_len] = '\0';

    /* 确认该 Inode 的最新版本仍在这个目录下且名称一致 */
    err = rofs__inode_find(fs, cand.inode, &latest);
    if (err != ROFS_OK) {
        return (err == ROFS_ERR_NOENT) ? ROFS_OK : err;
    }
    if (latest.parent != r->dir->inode ||
        latest.name_len != cand.name_len ||
        memcmp(latest.name, cand.name, cand.name_len) != 0) {
        return ROFS_OK;
    }
    /*
     * 只把“该 Inode 的最新记录”作为目录项返回。
     * 否则创建记录和后续更新记录会各返回一次，导致 readdir 出现重复项。
     */
    if (hdr->sequence != latest.rec_seq) {
        return ROFS_OK;
    }

    /* 选 sequence 最小的候选，保证按日志顺序遍历 */
    if (!r->found || hdr->sequence < r->best_seq) {
        r->found = 1u;
        r->best_seq = hdr->sequence;
        r->best = latest;
    }
    return ROFS_OK;
}

/*
 * 读取下一个目录项。
 * 返回 0：成功；1：结束；负值：错误。
 */
int rofs_readdir(rofs_dir_t *dir, rofs_dirent_t *entry)
{
    dir_read_ctx_t r;
    int err;

    if (dir == NULL || dir->used == 0u || entry == NULL) {
        return ROFS_ERR_BADF;
    }
    if (!dir->fs->mounted) {
        return ROFS_ERR_STATE;
    }

    rofs__lock(dir->fs);
    memset(&r, 0, sizeof(r));
    r.dir = dir;

    err = rofs__scan_all(dir->fs, dir_read_cb, &r);
    if (err != ROFS_OK) {
        rofs__unlock(dir->fs);
        return err;
    }
    if (!r.found) {
        rofs__unlock(dir->fs);
        return 1;                        /* 遍历结束 */
    }

    dir->iter_seq = r.best_seq;
    entry->inode = r.best.inode;
    entry->size = r.best.size;
    entry->type = r.best.type;
    memcpy(entry->name, r.best.name, r.best.name_len + 1u);
    rofs__unlock(dir->fs);
    return 0;
}

/* 关闭目录句柄 */
int rofs_closedir(rofs_dir_t *dir)
{
    if (dir == NULL || dir->used == 0u) {
        return ROFS_ERR_BADF;
    }
    rofs__lock(dir->fs);
    dir->used = 0u;
    rofs__unlock(dir->fs);
    return ROFS_OK;
}
