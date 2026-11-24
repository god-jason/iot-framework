/*
 * app_fs.c - PC Shell 的 MFS 文件镜像后端（MFS + VFS）
 * ============================================================================
 *
 * 用一个普通文件模拟 SPI NOR：
 *   - 块大小 4096B，镜像 512KB，与 f407ve 的分区参数一致；
 *   - read   ：fseek + fread；
 *   - program：模拟 NOR “只能 1 -> 0”，逐块读旧值后与新数据按位与再写回；
 *   - erase  ：写入 0xFF；
 *   - sync   ：fflush。
 *
 * 本文件把 MFS 适配为 fs_ops_t 并挂载到 VFS 根目录 "/"；
 * MFS 的 lock/unlock 在 mfs_config_t 中配置，适配层不重复加锁。
 */

#include "app_fs.h"

#include "ros.h"
#include "fs.h"
#include "fs_cmd.h"
#include "mfs.h"
#include "vfs.h"

#include <stdio.h>
#include <string.h>

#define IMG_SECTOR_SIZE   4096U
#define IMG_CHUNK_SIZE    512U   /* 模拟 NOR 读写时的临时块大小 */

static FILE *s_img;
static ros_mutex_t s_fs_lock;
static uint8_t s_ready;

/* ==========================================================================
 * MFS 适配层：mfs_* -> fs_ops_t
 * ========================================================================== */

typedef struct {
    mfs_t fs;
    mfs_blockdev_t dev;
    mfs_config_t cfg;
    uint8_t workbuf[MFS_WORKBUF_SIZE];
    uint8_t mounted;
    uint8_t auto_format;
} app_mfs_t;

static app_mfs_t s_mfs;

/* MFS 错误码 -> 通用 FS 错误码 */
static int mfs_map_err(int err)
{
    switch (err) {
    case MFS_OK:               return FS_OK;
    case MFS_ERR_PARAM:        return FS_ERR_INVAL;
    case MFS_ERR_IO:           return FS_ERR_IO;
    case MFS_ERR_CRC:          return FS_ERR_CORRUPT;
    case MFS_ERR_FORMAT:       return FS_ERR_FORMAT;
    case MFS_ERR_VERSION:      return FS_ERR_NOTSUP;
    case MFS_ERR_NOENT:        return FS_ERR_NOENT;
    case MFS_ERR_EXIST:        return FS_ERR_EXIST;
    case MFS_ERR_NOTDIR:       return FS_ERR_NOTDIR;
    case MFS_ERR_ISDIR:        return FS_ERR_ISDIR;
    case MFS_ERR_NOSPC:        return FS_ERR_NOSPC;
    case MFS_ERR_NOMEM:        return FS_ERR_NOMEM;
    case MFS_ERR_BUSY:         return FS_ERR_BUSY;
    case MFS_ERR_INVAL:        return FS_ERR_INVAL;
    case MFS_ERR_NOTEMPTY:     return FS_ERR_NOTEMPTY;
    case MFS_ERR_BADF:         return FS_ERR_BADF;
    case MFS_ERR_NOTSUP:       return FS_ERR_NOTSUP;
    case MFS_ERR_STATE:        return FS_ERR_STATE;
    default:                   return FS_ERR_IO;
    }
}

/* 通用打开标志 -> MFS 打开标志 */
static uint32_t mfs_map_flags(uint32_t flags)
{
    uint32_t out = flags & 3u;

    if ((flags & FS_O_CREAT) != 0u) {
        out |= MFS_O_CREAT;
    }
    if ((flags & FS_O_EXCL) != 0u) {
        out |= MFS_O_EXCL;
    }
    if ((flags & FS_O_TRUNC) != 0u) {
        out |= MFS_O_TRUNC;
    }
    if ((flags & FS_O_APPEND) != 0u) {
        out |= MFS_O_APPEND;
    }
    return out;
}

static int32_t mfs_map_io(int32_t n)
{
    return (n < 0) ? mfs_map_err(n) : n;
}

static int mfs_op_format(void *ctx)
{
    app_mfs_t *m = (app_mfs_t *)ctx;
    int err = mfs_format(&m->fs);

    if (err == MFS_OK) {
        m->mounted = 1u;
    }
    return mfs_map_err(err);
}

static int mfs_op_mount(void *ctx)
{
    app_mfs_t *m = (app_mfs_t *)ctx;
    int err = mfs_mount(&m->fs);

    if (err != MFS_OK && m->auto_format != 0u) {
        err = mfs_format(&m->fs);
    }
    if (err == MFS_OK) {
        m->mounted = 1u;
    }
    return mfs_map_err(err);
}

static int mfs_op_unmount(void *ctx)
{
    app_mfs_t *m = (app_mfs_t *)ctx;
    int err = mfs_unmount(&m->fs);

    m->mounted = 0u;
    return mfs_map_err(err);
}

static int mfs_op_statfs(void *ctx, fs_statfs_t *stat)
{
    app_mfs_t *m = (app_mfs_t *)ctx;
    mfs_statfs_t st;
    int err;

    if (stat == NULL) {
        return FS_ERR_INVAL;
    }
    err = mfs_statfs(&m->fs, &st);
    if (err != MFS_OK) {
        return mfs_map_err(err);
    }
    stat->total_bytes = st.total_bytes;
    stat->used_bytes = st.used_bytes;
    stat->free_bytes = st.free_bytes;
    stat->total_blocks = st.total_blocks;
    stat->used_blocks = (st.total_blocks >= st.free_blocks + st.bad_blocks)
                            ? st.total_blocks - st.free_blocks - st.bad_blocks
                            : 0u;
    stat->block_size = st.block_size;
    return FS_OK;
}

static int mfs_op_open(void *ctx, const char *path, uint32_t flags, void **fp)
{
    app_mfs_t *m = (app_mfs_t *)ctx;
    mfs_file_t *file = NULL;
    int err;

    if (path == NULL || fp == NULL) {
        return FS_ERR_INVAL;
    }
    err = mfs_open(&m->fs, path, mfs_map_flags(flags), &file);
    if (err != MFS_OK) {
        return mfs_map_err(err);
    }
    *fp = file;
    return FS_OK;
}

static int mfs_op_close(void *ctx, void *fp)
{
    (void)ctx;
    return mfs_map_err(mfs_close((mfs_file_t *)fp));
}

static int32_t mfs_op_read(void *ctx, void *fp, void *buf, uint32_t size)
{
    (void)ctx;
    return mfs_map_io((int32_t)mfs_read((mfs_file_t *)fp, buf, size));
}

static int32_t mfs_op_write(void *ctx, void *fp, const void *buf,
                            uint32_t size)
{
    (void)ctx;
    return mfs_map_io((int32_t)mfs_write((mfs_file_t *)fp, buf, size));
}

static int mfs_op_seek(void *ctx, void *fp, int32_t offset, int whence)
{
    (void)ctx;
    return mfs_map_err(mfs_seek((mfs_file_t *)fp, offset, whence));
}

static int32_t mfs_op_tell(void *ctx, void *fp)
{
    (void)ctx;
    return (int32_t)mfs_tell((mfs_file_t *)fp);
}

static uint32_t mfs_op_size(void *ctx, void *fp)
{
    (void)ctx;
    return (uint32_t)mfs_size((mfs_file_t *)fp);
}

static int mfs_op_stat(void *ctx, const char *path, fs_stat_t *stat)
{
    app_mfs_t *m = (app_mfs_t *)ctx;
    mfs_stat_t st;
    int err;

    if (path == NULL || stat == NULL) {
        return FS_ERR_INVAL;
    }
    err = mfs_stat(&m->fs, path, &st);
    if (err != MFS_OK) {
        return mfs_map_err(err);
    }
    stat->type = st.type;
    stat->size = st.size;
    strncpy(stat->name, st.name, FS_NAME_MAX);
    stat->name[FS_NAME_MAX] = '\0';
    return FS_OK;
}

static int mfs_op_mkdir(void *ctx, const char *path)
{
    app_mfs_t *m = (app_mfs_t *)ctx;

    return mfs_map_err(mfs_mkdir(&m->fs, path));
}

static int mfs_op_rmdir(void *ctx, const char *path)
{
    app_mfs_t *m = (app_mfs_t *)ctx;

    return mfs_map_err(mfs_rmdir(&m->fs, path));
}

static int mfs_op_unlink(void *ctx, const char *path)
{
    app_mfs_t *m = (app_mfs_t *)ctx;

    return mfs_map_err(mfs_unlink(&m->fs, path));
}

static int mfs_op_rename(void *ctx, const char *oldpath, const char *newpath)
{
    app_mfs_t *m = (app_mfs_t *)ctx;

    return mfs_map_err(mfs_rename(&m->fs, oldpath, newpath));
}

static int mfs_op_opendir(void *ctx, const char *path, void **dp)
{
    app_mfs_t *m = (app_mfs_t *)ctx;
    mfs_dir_t *dir = NULL;
    int err;

    if (path == NULL || dp == NULL) {
        return FS_ERR_INVAL;
    }
    err = mfs_opendir(&m->fs, path, &dir);
    if (err != MFS_OK) {
        return mfs_map_err(err);
    }
    *dp = dir;
    return FS_OK;
}

static int mfs_op_readdir(void *ctx, void *dp, fs_dirent_t *entry)
{
    (void)ctx;
    mfs_dirent_t ent;
    int err;

    if (entry == NULL) {
        return FS_ERR_INVAL;
    }
    err = mfs_readdir((mfs_dir_t *)dp, &ent);
    if (err < 0) {
        return mfs_map_err(err);
    }
    if (err > 0) {
        return 1;                        /* 遍历结束 */
    }
    entry->type = ent.type;
    entry->size = ent.size;
    strncpy(entry->name, ent.name, FS_NAME_MAX);
    entry->name[FS_NAME_MAX] = '\0';
    return 0;
}

static int mfs_op_closedir(void *ctx, void *dp)
{
    (void)ctx;
    return mfs_map_err(mfs_closedir((mfs_dir_t *)dp));
}

static const fs_ops_t s_mfs_ops = {
    .format = mfs_op_format,
    .mount = mfs_op_mount,
    .unmount = mfs_op_unmount,
    .statfs = mfs_op_statfs,
    .open = mfs_op_open,
    .close = mfs_op_close,
    .read = mfs_op_read,
    .write = mfs_op_write,
    .seek = mfs_op_seek,
    .tell = mfs_op_tell,
    .size = mfs_op_size,
    .stat = mfs_op_stat,
    .mkdir = mfs_op_mkdir,
    .rmdir = mfs_op_rmdir,
    .unlink = mfs_op_unlink,
    .rename = mfs_op_rename,
    .opendir = mfs_op_opendir,
    .readdir = mfs_op_readdir,
    .closedir = mfs_op_closedir
};

/* ==========================================================================
 * 文件镜像块设备
 * ========================================================================== */

/* 读取镜像区间 */
static int img_read(void *ctx, uint32_t addr, void *buffer, uint32_t size)
{
    (void)ctx;
    if (s_img == NULL || buffer == NULL) {
        return -1;
    }
    if (fseek(s_img, (long)addr, SEEK_SET) != 0) {
        return -1;
    }
    if (fread(buffer, 1U, size, s_img) != size) {
        return -1;
    }
    return 0;
}

/*
 * 模拟 NOR 编程：新数据只能把已有位从 1 清成 0。
 * 逐块读出旧内容，与新数据按位与后写回。
 */
static int img_program(void *ctx, uint32_t addr,
                       const void *buffer, uint32_t size)
{
    static uint8_t tmp[IMG_CHUNK_SIZE];
    const uint8_t *src = (const uint8_t *)buffer;
    uint32_t left = size;
    uint32_t cur = addr;
    uint32_t i;
    uint32_t n;

    (void)ctx;
    if (s_img == NULL || buffer == NULL) {
        return -1;
    }

    while (left > 0U) {
        n = (left > sizeof(tmp)) ? (uint32_t)sizeof(tmp) : left;

        if (fseek(s_img, (long)cur, SEEK_SET) != 0) {
            return -1;
        }
        if (fread(tmp, 1U, n, s_img) != n) {
            return -1;
        }
        for (i = 0U; i < n; i++) {
            tmp[i] &= src[i];
        }
        /* 更新流在读写之间必须重新定位 */
        if (fseek(s_img, (long)cur, SEEK_SET) != 0) {
            return -1;
        }
        if (fwrite(tmp, 1U, n, s_img) != n) {
            return -1;
        }

        cur += n;
        src += n;
        left -= n;
    }
    return 0;
}

/* 擦除：写入 0xFF */
static int img_erase(void *ctx, uint32_t addr, uint32_t size)
{
    static uint8_t ff[IMG_CHUNK_SIZE];
    uint32_t left = size;
    uint32_t cur = addr;
    uint32_t n;

    (void)ctx;
    if (s_img == NULL) {
        return -1;
    }

    memset(ff, 0xFF, sizeof(ff));
    while (left > 0U) {
        n = (left > sizeof(ff)) ? (uint32_t)sizeof(ff) : left;
        if (fseek(s_img, (long)cur, SEEK_SET) != 0) {
            return -1;
        }
        if (fwrite(ff, 1U, n, s_img) != n) {
            return -1;
        }
        cur += n;
        left -= n;
    }
    return 0;
}

/* 同步文件内容 */
static int img_sync(void *ctx)
{
    (void)ctx;
    if (s_img == NULL) {
        return -1;
    }
    return (fflush(s_img) == 0) ? 0 : -1;
}

/* ---------------- 锁 ---------------- */

static void fs_lock(void *ctx)
{
    (void)ctx;
    (void)ros_mutex_lock(&s_fs_lock, ROS_TIMEOUT_FOREVER);
}

static void fs_unlock(void *ctx)
{
    (void)ctx;
    (void)ros_mutex_unlock(&s_fs_lock);
}

/*
 * 打开/创建镜像。
 * 文件不足 APP_FS_IMG_SIZE 时扩展到目标大小（末字节写 0xFF）。
 */
static int img_open(const char *path)
{
    long size;

    s_img = fopen(path, "r+b");
    if (s_img == NULL) {
        s_img = fopen(path, "w+b");
    }
    if (s_img == NULL) {
        ros_printf("fs: open '%s' failed\n", path);
        return -1;
    }

    if (fseek(s_img, 0L, SEEK_END) != 0) {
        return -1;
    }
    size = ftell(s_img);
    if (size < (long)APP_FS_IMG_SIZE) {
        if (fseek(s_img, (long)APP_FS_IMG_SIZE - 1L, SEEK_SET) != 0) {
            return -1;
        }
        if (fputc(0xFF, s_img) == EOF) {
            return -1;
        }
        if (fflush(s_img) != 0) {
            return -1;
        }
    }
    return 0;
}

int app_fs_init(const char *img_path)
{
    fs_statfs_t st;
    int rc;

    if (s_img != NULL) {
        return 0;                        /* 已初始化 */
    }
    if (img_path == NULL) {
        img_path = APP_FS_IMG_PATH;
    }

    (void)ros_mutex_init(&s_fs_lock);

    if (img_open(img_path) != 0) {
        if (s_img != NULL) {
            (void)fclose(s_img);
            s_img = NULL;
        }
        return -1;
    }

    memset(&s_mfs, 0, sizeof(s_mfs));
    s_mfs.auto_format = 1u;

    s_mfs.dev.read = img_read;
    s_mfs.dev.program = img_program;
    s_mfs.dev.erase = img_erase;
    s_mfs.dev.sync = img_sync;
    s_mfs.dev.start_addr = 0u;
    s_mfs.dev.total_size = APP_FS_IMG_SIZE;
    s_mfs.dev.erase_size = IMG_SECTOR_SIZE;
    s_mfs.dev.program_size = 1u;
    s_mfs.dev.read_size = 1u;

    s_mfs.cfg.workbuf = s_mfs.workbuf;
    s_mfs.cfg.workbuf_size = sizeof(s_mfs.workbuf);
    s_mfs.cfg.max_open_files = MFS_MAX_OPEN_FILES;
    s_mfs.cfg.max_open_dirs = MFS_MAX_OPEN_DIRS;
    s_mfs.cfg.auto_gc = 1u;
    s_mfs.cfg.lock = fs_lock;
    s_mfs.cfg.unlock = fs_unlock;

    rc = mfs_map_err(mfs_init(&s_mfs.fs, &s_mfs.dev, &s_mfs.cfg));
    if (rc != FS_OK) {
        ros_printf("fs: mfs init failed (%d)\n", rc);
        (void)app_fs_deinit();
        return -1;
    }

    rc = vfs_mount("/", &s_mfs_ops, &s_mfs);
    if (rc != FS_OK) {
        ros_printf("fs: mount failed (%d)\n", rc);
        (void)app_fs_deinit();
        return -1;
    }

    if (vfs_statfs("/", &st) == FS_OK) {
        ros_printf("fs: %s mounted, %u/%u KB free\n", img_path,
                   (unsigned)(st.free_bytes / 1024U),
                   (unsigned)(st.total_bytes / 1024U));
    } else {
        ros_printf("fs: %s mounted\n", img_path);
    }

    (void)fs_cmd_register();
    s_ready = 1u;
    return 0;
}

void app_fs_deinit(void)
{
    if (s_img == NULL) {
        return;
    }
    if (s_ready != 0u) {
        (void)vfs_umount("/");
        s_ready = 0u;
    }
    (void)fclose(s_img);
    s_img = NULL;
}

int app_fs_ready(void)
{
    return (s_ready != 0u) ? 0 : -1;
}
