#include "fs.h"

#include <string.h>

static lfs_t s_lfs;
static const struct lfs_config *s_cfg;
static bool s_mounted;

static bool fs_ready(void)
{
    return s_mounted;
}

int fs_init(const fs_config_t *cfg)
{
    int err;

    if (cfg == NULL || cfg->lfs_cfg == NULL) {
        return LFS_ERR_INVAL;
    }
    if (cfg->lfs_cfg->read == NULL || cfg->lfs_cfg->prog == NULL ||
        cfg->lfs_cfg->erase == NULL) {
        return LFS_ERR_INVAL;
    }
    /* 文件打开使用 fs_file_t 内置缓存，块设备缓存不能超过该大小 */
    if (cfg->lfs_cfg->cache_size > FS_FILE_CACHE_SIZE) {
        return LFS_ERR_INVAL;
    }
    if (s_mounted) {
        return 0;
    }

    s_cfg = cfg->lfs_cfg;
    err = lfs_mount(&s_lfs, s_cfg);
    if (err != 0 && cfg->auto_format) {
        err = lfs_format(&s_lfs, s_cfg);
        if (err == 0) {
            err = lfs_mount(&s_lfs, s_cfg);
        }
    }
    if (err != 0) {
        s_cfg = NULL;
        return err;
    }
    s_mounted = true;
    return 0;
}

int fs_deinit(void)
{
    if (!s_mounted) {
        return 0;
    }
    (void)lfs_unmount(&s_lfs);
    s_mounted = false;
    s_cfg = NULL;
    return 0;
}

int fs_mount(void)
{
    int err;

    if (s_mounted) {
        return 0;
    }
    if (s_cfg == NULL) {
        return LFS_ERR_INVAL;
    }
    err = lfs_mount(&s_lfs, s_cfg);
    if (err == 0) {
        s_mounted = true;
    }
    return err;
}

int fs_unmount(void)
{
    int err;

    if (!s_mounted) {
        return 0;
    }
    err = lfs_unmount(&s_lfs);
    if (err == 0) {
        s_mounted = false;
    }
    return err;
}

int fs_format(void)
{
    int err;

    if (s_cfg == NULL) {
        return LFS_ERR_INVAL;
    }
    if (s_mounted) {
        (void)fs_unmount();
    }
    err = lfs_format(&s_lfs, s_cfg);
    if (err != 0) {
        return err;
    }
    return fs_mount();
}

bool fs_is_mounted(void)
{
    return fs_ready();
}

int fs_open(fs_file_t *file, const char *path, int flags)
{
    if (!fs_ready() || file == NULL || path == NULL) {
        return LFS_ERR_INVAL;
    }

    memset(&file->cfg, 0, sizeof(file->cfg));
    file->cfg.buffer = file->cache;
    return lfs_file_opencfg(&s_lfs, &file->file, path, flags, &file->cfg);
}

int fs_close(fs_file_t *file)
{
    if (!fs_ready() || file == NULL) {
        return LFS_ERR_INVAL;
    }
    return lfs_file_close(&s_lfs, &file->file);
}

int fs_read(fs_file_t *file, void *buf, size_t len)
{
    lfs_ssize_t n;

    if (!fs_ready() || file == NULL || (buf == NULL && len > 0U)) {
        return LFS_ERR_INVAL;
    }
    n = lfs_file_read(&s_lfs, &file->file, buf, (lfs_size_t)len);
    return (int)n;
}

int fs_write(fs_file_t *file, const void *buf, size_t len)
{
    lfs_ssize_t n;

    if (!fs_ready() || file == NULL || (buf == NULL && len > 0U)) {
        return LFS_ERR_INVAL;
    }
    n = lfs_file_write(&s_lfs, &file->file, buf, (lfs_size_t)len);
    return (int)n;
}

int fs_seek(fs_file_t *file, int32_t offset, int whence)
{
    lfs_soff_t pos;

    if (!fs_ready() || file == NULL) {
        return LFS_ERR_INVAL;
    }
    pos = lfs_file_seek(&s_lfs, &file->file, (lfs_soff_t)offset, whence);
    return (int)pos;
}

int32_t fs_tell(fs_file_t *file)
{
    if (!fs_ready() || file == NULL) {
        return LFS_ERR_INVAL;
    }
    return (int32_t)lfs_file_tell(&s_lfs, &file->file);
}

int32_t fs_size(fs_file_t *file)
{
    if (!fs_ready() || file == NULL) {
        return LFS_ERR_INVAL;
    }
    return (int32_t)lfs_file_size(&s_lfs, &file->file);
}

int fs_sync(fs_file_t *file)
{
    if (!fs_ready() || file == NULL) {
        return LFS_ERR_INVAL;
    }
    return lfs_file_sync(&s_lfs, &file->file);
}

int fs_remove(const char *path)
{
    if (!fs_ready() || path == NULL) {
        return LFS_ERR_INVAL;
    }
    return lfs_remove(&s_lfs, path);
}

int fs_rename(const char *oldpath, const char *newpath)
{
    if (!fs_ready() || oldpath == NULL || newpath == NULL) {
        return LFS_ERR_INVAL;
    }
    return lfs_rename(&s_lfs, oldpath, newpath);
}

int fs_mkdir(const char *path)
{
    if (!fs_ready() || path == NULL) {
        return LFS_ERR_INVAL;
    }
    return lfs_mkdir(&s_lfs, path);
}

int fs_stat(const char *path, fs_info_t *info)
{
    struct lfs_info li;
    int err;

    if (!fs_ready() || path == NULL || info == NULL) {
        return LFS_ERR_INVAL;
    }
    err = lfs_stat(&s_lfs, path, &li);
    if (err != 0) {
        return err;
    }
    info->size = li.size;
    info->type = (li.type == LFS_TYPE_DIR) ? FS_TYPE_DIR : FS_TYPE_FILE;
    strncpy(info->name, li.name, LFS_NAME_MAX);
    info->name[LFS_NAME_MAX] = '\0';
    return 0;
}

int fs_dir_open(fs_dir_t *dir, const char *path)
{
    if (!fs_ready() || dir == NULL || path == NULL) {
        return LFS_ERR_INVAL;
    }
    return lfs_dir_open(&s_lfs, &dir->dir, path);
}

int fs_dir_read(fs_dir_t *dir, fs_info_t *info)
{
    struct lfs_info li;
    int err;

    if (!fs_ready() || dir == NULL || info == NULL) {
        return LFS_ERR_INVAL;
    }
    err = lfs_dir_read(&s_lfs, &dir->dir, &li);
    if (err <= 0) {
        return err;
    }
    info->size = li.size;
    info->type = (li.type == LFS_TYPE_DIR) ? FS_TYPE_DIR : FS_TYPE_FILE;
    strncpy(info->name, li.name, LFS_NAME_MAX);
    info->name[LFS_NAME_MAX] = '\0';
    return 1;
}

int fs_dir_close(fs_dir_t *dir)
{
    if (!fs_ready() || dir == NULL) {
        return LFS_ERR_INVAL;
    }
    return lfs_dir_close(&s_lfs, &dir->dir);
}
