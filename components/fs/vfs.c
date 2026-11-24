/*
 * vfs.c - 虚拟文件系统：挂载表与路径前缀分发
 * ============================================================================
 *
 * 每个挂载点保存 {前缀, 后端操作表, 后端实例}。
 * 访问路径时按最长前缀匹配，去掉挂载前缀后交给后端。
 *
 * 本实现不做引用计数、不限制打开句柄数量：
 *   - vfs_file_t / vfs_dir_t 由调用者分配；
 *   - 卸载时若仍有句柄打开，由调用者保证不再使用。
 */

#include "vfs.h"

#include <string.h>

struct vfs_mount {
    uint8_t used;
    char mp[VFS_MOUNT_MAX];
    const fs_ops_t *ops;
    void *ctx;
};

static struct vfs_mount s_mounts[VFS_MAX_MOUNTS];

/* ---------------- 内部工具 ---------------- */

/* 挂载点合法性：'/' 开头，除根外不以 '/' 结尾，长度受限 */
static int mount_point_valid(const char *mp)
{
    size_t len;

    if (mp == NULL || mp[0] != '/') {
        return 0;
    }
    len = strlen(mp);
    if (len == 0u || len >= VFS_MOUNT_MAX) {
        return 0;
    }
    if (len > 1u && mp[len - 1u] == '/') {
        return 0;
    }
    return 1;
}

/* 精确查找挂载点 */
static struct vfs_mount *mount_find(const char *mp)
{
    uint32_t i;

    for (i = 0u; i < VFS_MAX_MOUNTS; i++) {
        if (s_mounts[i].used != 0u && strcmp(s_mounts[i].mp, mp) == 0) {
            return &s_mounts[i];
        }
    }
    return NULL;
}

/*
 * 按最长前缀匹配找到挂载点，并生成后端相对路径。
 * 相对路径最多 FS_PATH_MAX 字节。
 */
static int path_resolve(const char *path, struct vfs_mount **out,
                        char *rel, size_t cap)
{
    struct vfs_mount *best = NULL;
    size_t best_len = 0u;
    uint32_t i;

    if (path == NULL || path[0] != '/') {
        return FS_ERR_INVAL;
    }

    for (i = 0u; i < VFS_MAX_MOUNTS; i++) {
        struct vfs_mount *m = &s_mounts[i];
        size_t len;

        if (m->used == 0u) {
            continue;
        }
        len = strlen(m->mp);
        if (len > 1u) {
            if (strncmp(path, m->mp, len) != 0) {
                continue;
            }
            if (path[len] != '\0' && path[len] != '/') {
                continue;
            }
        }
        /* 根挂载点匹配所有绝对路径 */
        if (best == NULL || len > best_len) {
            best = m;
            best_len = len;
        }
    }

    if (best == NULL) {
        return FS_ERR_NOENT;
    }

    if (best_len <= 1u) {
        if (strlen(path) >= cap) {
            return FS_ERR_NAMETOOLONG;
        }
        strcpy(rel, path);
    } else {
        const char *sub = path + best_len;

        if (*sub == '\0') {
            sub = "/";
        }
        if (strlen(sub) >= cap) {
            return FS_ERR_NAMETOOLONG;
        }
        strcpy(rel, sub);
    }

    *out = best;
    return FS_OK;
}

static int file_valid(const vfs_file_t *file)
{
    return (file != NULL && file->used != 0u && file->mnt != NULL &&
            file->mnt->used != 0u);
}

static int dir_valid(const vfs_dir_t *dir)
{
    return (dir != NULL && dir->used != 0u && dir->mnt != NULL &&
            dir->mnt->used != 0u);
}

/* ---------------- 挂载管理 ---------------- */

int vfs_mount(const char *mp, const fs_ops_t *ops, void *ctx)
{
    struct vfs_mount *slot = NULL;
    uint32_t i;
    int err;

    if (mp == NULL || ops == NULL || ops->mount == NULL) {
        return FS_ERR_INVAL;
    }
    if (!mount_point_valid(mp)) {
        return FS_ERR_INVAL;
    }
    if (mount_find(mp) != NULL) {
        return FS_ERR_EXIST;
    }

    for (i = 0u; i < VFS_MAX_MOUNTS; i++) {
        if (s_mounts[i].used == 0u) {
            slot = &s_mounts[i];
            break;
        }
    }
    if (slot == NULL) {
        return FS_ERR_NOSPC;
    }

    err = ops->mount(ctx);
    if (err != FS_OK) {
        return err;
    }

    memset(slot, 0, sizeof(*slot));
    slot->used = 1u;
    strncpy(slot->mp, mp, sizeof(slot->mp) - 1u);
    slot->ops = ops;
    slot->ctx = ctx;
    return FS_OK;
}

int vfs_umount(const char *mp)
{
    struct vfs_mount *m;
    int err;

    if (mp == NULL) {
        return FS_ERR_INVAL;
    }
    m = mount_find(mp);
    if (m == NULL) {
        return FS_ERR_NOENT;
    }

    err = (m->ops->unmount != NULL) ? m->ops->unmount(m->ctx) : FS_OK;
    m->used = 0u;
    return err;
}

int vfs_format(const char *mp)
{
    struct vfs_mount *m;

    if (mp == NULL) {
        return FS_ERR_INVAL;
    }
    m = mount_find(mp);
    if (m == NULL) {
        return FS_ERR_NOENT;
    }
    if (m->ops->format == NULL) {
        return FS_ERR_NOTSUP;
    }
    return m->ops->format(m->ctx);
}

int vfs_statfs(const char *path, fs_statfs_t *stat)
{
    struct vfs_mount *m;
    char rel[FS_PATH_MAX + 1];
    int err;

    if (stat == NULL) {
        return FS_ERR_INVAL;
    }
    err = path_resolve(path, &m, rel, sizeof(rel));
    if (err != FS_OK) {
        return err;
    }
    if (m->ops->statfs == NULL) {
        return FS_ERR_NOTSUP;
    }
    return m->ops->statfs(m->ctx, stat);
}

/* ---------------- 文件操作 ---------------- */

int vfs_open(const char *path, uint32_t flags, vfs_file_t *file)
{
    struct vfs_mount *m;
    char rel[FS_PATH_MAX + 1];
    void *fp = NULL;
    int err;

    if (file == NULL || file->used != 0u) {
        return FS_ERR_INVAL;
    }
    err = path_resolve(path, &m, rel, sizeof(rel));
    if (err != FS_OK) {
        return err;
    }
    if (m->ops->open == NULL) {
        return FS_ERR_NOTSUP;
    }
    err = m->ops->open(m->ctx, rel, flags, &fp);
    if (err != FS_OK) {
        return err;
    }
    file->mnt = m;
    file->fp = fp;
    file->flags = flags;
    file->used = 1u;
    return FS_OK;
}

int vfs_close(vfs_file_t *file)
{
    int err;

    if (!file_valid(file)) {
        return FS_ERR_BADF;
    }
    err = (file->mnt->ops->close != NULL)
              ? file->mnt->ops->close(file->mnt->ctx, file->fp) : FS_OK;
    file->used = 0u;
    return err;
}

int32_t vfs_read(vfs_file_t *file, void *buf, uint32_t size)
{
    if (!file_valid(file) || file->mnt->ops->read == NULL) {
        return FS_ERR_BADF;
    }
    return file->mnt->ops->read(file->mnt->ctx, file->fp, buf, size);
}

int32_t vfs_write(vfs_file_t *file, const void *buf, uint32_t size)
{
    if (!file_valid(file) || file->mnt->ops->write == NULL) {
        return FS_ERR_BADF;
    }
    return file->mnt->ops->write(file->mnt->ctx, file->fp, buf, size);
}

int vfs_seek(vfs_file_t *file, int32_t offset, int whence)
{
    if (!file_valid(file) || file->mnt->ops->seek == NULL) {
        return FS_ERR_BADF;
    }
    return file->mnt->ops->seek(file->mnt->ctx, file->fp, offset, whence);
}

int32_t vfs_tell(vfs_file_t *file)
{
    if (!file_valid(file) || file->mnt->ops->tell == NULL) {
        return FS_ERR_BADF;
    }
    return file->mnt->ops->tell(file->mnt->ctx, file->fp);
}

uint32_t vfs_size(vfs_file_t *file)
{
    if (!file_valid(file) || file->mnt->ops->size == NULL) {
        return 0u;
    }
    return file->mnt->ops->size(file->mnt->ctx, file->fp);
}

/* ---------------- 命名空间操作 ---------------- */

int vfs_stat(const char *path, fs_stat_t *stat)
{
    struct vfs_mount *m;
    char rel[FS_PATH_MAX + 1];
    int err;

    if (stat == NULL) {
        return FS_ERR_INVAL;
    }
    err = path_resolve(path, &m, rel, sizeof(rel));
    if (err != FS_OK) {
        return err;
    }
    if (m->ops->stat == NULL) {
        return FS_ERR_NOTSUP;
    }
    return m->ops->stat(m->ctx, rel, stat);
}

int vfs_exists(const char *path)
{
    fs_stat_t st;
    int err = vfs_stat(path, &st);

    if (err == FS_OK) {
        return 1;
    }
    return (err == FS_ERR_NOENT) ? 0 : err;
}

int vfs_isdir(const char *path)
{
    fs_stat_t st;
    int err = vfs_stat(path, &st);

    if (err == FS_OK) {
        return (st.type == FS_TYPE_DIR) ? 1 : 0;
    }
    return (err == FS_ERR_NOENT) ? 0 : err;
}

int vfs_mkdir(const char *path)
{
    struct vfs_mount *m;
    char rel[FS_PATH_MAX + 1];
    int err;

    err = path_resolve(path, &m, rel, sizeof(rel));
    if (err != FS_OK) {
        return err;
    }
    if (m->ops->mkdir == NULL) {
        return FS_ERR_NOTSUP;
    }
    return m->ops->mkdir(m->ctx, rel);
}

int vfs_rmdir(const char *path)
{
    struct vfs_mount *m;
    char rel[FS_PATH_MAX + 1];
    int err;

    err = path_resolve(path, &m, rel, sizeof(rel));
    if (err != FS_OK) {
        return err;
    }
    if (m->ops->rmdir == NULL) {
        return FS_ERR_NOTSUP;
    }
    return m->ops->rmdir(m->ctx, rel);
}

int vfs_unlink(const char *path)
{
    struct vfs_mount *m;
    char rel[FS_PATH_MAX + 1];
    int err;

    err = path_resolve(path, &m, rel, sizeof(rel));
    if (err != FS_OK) {
        return err;
    }
    if (m->ops->unlink == NULL) {
        return FS_ERR_NOTSUP;
    }
    return m->ops->unlink(m->ctx, rel);
}

int vfs_rename(const char *oldpath, const char *newpath)
{
    struct vfs_mount *om;
    struct vfs_mount *nm;
    char orel[FS_PATH_MAX + 1];
    char nrel[FS_PATH_MAX + 1];
    int err;

    err = path_resolve(oldpath, &om, orel, sizeof(orel));
    if (err != FS_OK) {
        return err;
    }
    err = path_resolve(newpath, &nm, nrel, sizeof(nrel));
    if (err != FS_OK) {
        return err;
    }
    if (om != nm) {
        return FS_ERR_NOTSUP;            /* 不支持跨挂载点重命名 */
    }
    if (om->ops->rename == NULL) {
        return FS_ERR_NOTSUP;
    }
    return om->ops->rename(om->ctx, orel, nrel);
}

/* ---------------- 目录遍历 ---------------- */

int vfs_opendir(const char *path, vfs_dir_t *dir)
{
    struct vfs_mount *m;
    char rel[FS_PATH_MAX + 1];
    void *dp = NULL;
    int err;

    if (dir == NULL || dir->used != 0u) {
        return FS_ERR_INVAL;
    }
    err = path_resolve(path, &m, rel, sizeof(rel));
    if (err != FS_OK) {
        return err;
    }
    if (m->ops->opendir == NULL) {
        return FS_ERR_NOTSUP;
    }
    err = m->ops->opendir(m->ctx, rel, &dp);
    if (err != FS_OK) {
        return err;
    }
    dir->mnt = m;
    dir->dp = dp;
    dir->used = 1u;
    return FS_OK;
}

int vfs_readdir(vfs_dir_t *dir, fs_dirent_t *entry)
{
    if (!dir_valid(dir) || dir->mnt->ops->readdir == NULL || entry == NULL) {
        return FS_ERR_BADF;
    }
    return dir->mnt->ops->readdir(dir->mnt->ctx, dir->dp, entry);
}

int vfs_closedir(vfs_dir_t *dir)
{
    int err;

    if (!dir_valid(dir)) {
        return FS_ERR_BADF;
    }
    err = (dir->mnt->ops->closedir != NULL)
              ? dir->mnt->ops->closedir(dir->mnt->ctx, dir->dp) : FS_OK;
    dir->used = 0u;
    return err;
}
