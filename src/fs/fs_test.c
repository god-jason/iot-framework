/*
 * fs_test.c - VFS + MFS 适配层测试
 * ============================================================================
 *
 * 两块 64KB RAM 盘（16 x 4KB，模拟 NOR 1->0 编程），
 * 分别把 MFS 挂载到 "/a" 与 "/b"，验证：
 *   - VFS 多挂载点前缀分发；
 *   - 文件/目录接口与 MFS 适配层；
 *   - 跨挂载点重命名拒绝；
 *   - 卸载/重挂载持久化。
 */

#include "test.h"
#include "ros.h"
#include "fs.h"
#include "mfs.h"
#include "vfs.h"
#include "fs_cmd.h"
#include "roshell.h"
#include "roshell_repl.h"
#include "roshell_fs.h"

#include <string.h>

#define T_BLOCK_SIZE   4096u
#define T_BLOCK_COUNT  16u
#define T_DISK_SIZE    (T_BLOCK_SIZE * T_BLOCK_COUNT)

static uint8_t s_disk[T_DISK_SIZE];
static uint8_t s_disk_b[T_DISK_SIZE];

static bool s_cmd_ready;        /* fs 命令是否已注册（全局命令表只允许注册一次） */

/* ---------------- RAM 块设备 ---------------- */

static int disk_read(void *ctx, uint32_t addr, void *buf, uint32_t size)
{
    const uint8_t *disk = (const uint8_t *)ctx;

    if (addr > T_DISK_SIZE || size > T_DISK_SIZE - addr) {
        return -1;
    }
    memcpy(buf, &disk[addr], size);
    return 0;
}

static int disk_program(void *ctx, uint32_t addr, const void *buf,
                        uint32_t size)
{
    uint8_t *disk = (uint8_t *)ctx;
    const uint8_t *src = (const uint8_t *)buf;
    uint32_t i;

    if (addr > T_DISK_SIZE || size > T_DISK_SIZE - addr) {
        return -1;
    }
    for (i = 0u; i < size; i++) {
        disk[addr + i] &= src[i];
    }
    return 0;
}

static int disk_erase(void *ctx, uint32_t addr, uint32_t size)
{
    uint8_t *disk = (uint8_t *)ctx;

    if (addr > T_DISK_SIZE || size > T_DISK_SIZE - addr ||
        (addr % T_BLOCK_SIZE) != 0u || (size % T_BLOCK_SIZE) != 0u) {
        return -1;
    }
    memset(&disk[addr], 0xFFu, size);
    return 0;
}

static int disk_sync(void *ctx)
{
    (void)ctx;
    return 0;
}

/* ---------------- MFS 适配层（测试专用） ---------------- */

typedef struct {
    mfs_t fs;
    mfs_blockdev_t dev;
    mfs_config_t cfg;
    uint8_t workbuf[MFS_WORKBUF_SIZE];
    uint8_t mounted;
    uint8_t auto_format;
} test_mfs_t;

static int map_err(int err)
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

static uint32_t map_flags(uint32_t flags)
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

static int32_t map_io(int32_t n)
{
    return (n < 0) ? map_err(n) : n;
}

static int op_format(void *ctx)
{
    test_mfs_t *m = (test_mfs_t *)ctx;
    int err = mfs_format(&m->fs);

    if (err == MFS_OK) {
        m->mounted = 1u;
    }
    return map_err(err);
}

static int op_mount(void *ctx)
{
    test_mfs_t *m = (test_mfs_t *)ctx;
    int err = mfs_mount(&m->fs);

    if (err != MFS_OK && m->auto_format != 0u) {
        err = mfs_format(&m->fs);
    }
    if (err == MFS_OK) {
        m->mounted = 1u;
    }
    return map_err(err);
}

static int op_unmount(void *ctx)
{
    test_mfs_t *m = (test_mfs_t *)ctx;
    int err = mfs_unmount(&m->fs);

    m->mounted = 0u;
    return map_err(err);
}

static int op_statfs(void *ctx, fs_statfs_t *stat)
{
    test_mfs_t *m = (test_mfs_t *)ctx;
    mfs_statfs_t st;
    int err;

    if (stat == NULL) {
        return FS_ERR_INVAL;
    }
    err = mfs_statfs(&m->fs, &st);
    if (err != MFS_OK) {
        return map_err(err);
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

static int op_open(void *ctx, const char *path, uint32_t flags, void **fp)
{
    test_mfs_t *m = (test_mfs_t *)ctx;
    mfs_file_t *file = NULL;
    int err;

    if (path == NULL || fp == NULL) {
        return FS_ERR_INVAL;
    }
    err = mfs_open(&m->fs, path, map_flags(flags), &file);
    if (err != MFS_OK) {
        return map_err(err);
    }
    *fp = file;
    return FS_OK;
}

static int op_close(void *ctx, void *fp)
{
    (void)ctx;
    return map_err(mfs_close((mfs_file_t *)fp));
}

static int32_t op_read(void *ctx, void *fp, void *buf, uint32_t size)
{
    (void)ctx;
    return map_io((int32_t)mfs_read((mfs_file_t *)fp, buf, size));
}

static int32_t op_write(void *ctx, void *fp, const void *buf, uint32_t size)
{
    (void)ctx;
    return map_io((int32_t)mfs_write((mfs_file_t *)fp, buf, size));
}

static int op_seek(void *ctx, void *fp, int32_t offset, int whence)
{
    (void)ctx;
    return map_err(mfs_seek((mfs_file_t *)fp, offset, whence));
}

static int32_t op_tell(void *ctx, void *fp)
{
    (void)ctx;
    return (int32_t)mfs_tell((mfs_file_t *)fp);
}

static uint32_t op_size(void *ctx, void *fp)
{
    (void)ctx;
    return (uint32_t)mfs_size((mfs_file_t *)fp);
}

static int op_stat(void *ctx, const char *path, fs_stat_t *stat)
{
    test_mfs_t *m = (test_mfs_t *)ctx;
    mfs_stat_t st;
    int err;

    if (path == NULL || stat == NULL) {
        return FS_ERR_INVAL;
    }
    err = mfs_stat(&m->fs, path, &st);
    if (err != MFS_OK) {
        return map_err(err);
    }
    stat->type = st.type;
    stat->size = st.size;
    strncpy(stat->name, st.name, FS_NAME_MAX);
    stat->name[FS_NAME_MAX] = '\0';
    return FS_OK;
}

static int op_mkdir(void *ctx, const char *path)
{
    test_mfs_t *m = (test_mfs_t *)ctx;

    return map_err(mfs_mkdir(&m->fs, path));
}

static int op_rmdir(void *ctx, const char *path)
{
    test_mfs_t *m = (test_mfs_t *)ctx;

    return map_err(mfs_rmdir(&m->fs, path));
}

static int op_unlink(void *ctx, const char *path)
{
    test_mfs_t *m = (test_mfs_t *)ctx;

    return map_err(mfs_unlink(&m->fs, path));
}

static int op_rename(void *ctx, const char *oldpath, const char *newpath)
{
    test_mfs_t *m = (test_mfs_t *)ctx;

    return map_err(mfs_rename(&m->fs, oldpath, newpath));
}

static int op_opendir(void *ctx, const char *path, void **dp)
{
    test_mfs_t *m = (test_mfs_t *)ctx;
    mfs_dir_t *dir = NULL;
    int err;

    if (path == NULL || dp == NULL) {
        return FS_ERR_INVAL;
    }
    err = mfs_opendir(&m->fs, path, &dir);
    if (err != MFS_OK) {
        return map_err(err);
    }
    *dp = dir;
    return FS_OK;
}

static int op_readdir(void *ctx, void *dp, fs_dirent_t *entry)
{
    (void)ctx;
    mfs_dirent_t ent;
    int err;

    if (entry == NULL) {
        return FS_ERR_INVAL;
    }
    err = mfs_readdir((mfs_dir_t *)dp, &ent);
    if (err < 0) {
        return map_err(err);
    }
    if (err > 0) {
        return 1;
    }
    entry->type = ent.type;
    entry->size = ent.size;
    strncpy(entry->name, ent.name, FS_NAME_MAX);
    entry->name[FS_NAME_MAX] = '\0';
    return 0;
}

static int op_closedir(void *ctx, void *dp)
{
    (void)ctx;
    return map_err(mfs_closedir((mfs_dir_t *)dp));
}

static const fs_ops_t s_test_ops = {
    .format = op_format,
    .mount = op_mount,
    .unmount = op_unmount,
    .statfs = op_statfs,
    .open = op_open,
    .close = op_close,
    .read = op_read,
    .write = op_write,
    .seek = op_seek,
    .tell = op_tell,
    .size = op_size,
    .stat = op_stat,
    .mkdir = op_mkdir,
    .rmdir = op_rmdir,
    .unlink = op_unlink,
    .rename = op_rename,
    .opendir = op_opendir,
    .readdir = op_readdir,
    .closedir = op_closedir
};

/* 初始化一个测试 MFS 实例（disks 为对应 RAM 盘） */
static int test_mfs_setup(test_mfs_t *m, uint8_t *disk)
{
    int err;

    memset(m, 0, sizeof(*m));
    m->auto_format = 1u;

    m->dev.read = disk_read;
    m->dev.program = disk_program;
    m->dev.erase = disk_erase;
    m->dev.sync = disk_sync;
    m->dev.ctx = disk;
    m->dev.start_addr = 0u;
    m->dev.total_size = T_DISK_SIZE;
    m->dev.erase_size = T_BLOCK_SIZE;
    m->dev.program_size = 1u;
    m->dev.read_size = 1u;

    m->cfg.workbuf = m->workbuf;
    m->cfg.workbuf_size = sizeof(m->workbuf);
    m->cfg.max_open_files = MFS_MAX_OPEN_FILES;
    m->cfg.max_open_dirs = MFS_MAX_OPEN_DIRS;
    m->cfg.auto_gc = 1u;

    err = mfs_init(&m->fs, &m->dev, &m->cfg);
    return map_err(err);
}

/* 读取整个文件，返回字节数或负错误码 */
static int read_all(const char *path, void *buf, uint32_t cap)
{
    vfs_file_t f;
    int32_t n;

    memset(&f, 0, sizeof(f));
    if (vfs_open(path, FS_O_RDONLY, &f) != FS_OK) {
        return FS_ERR_IO;
    }
    n = vfs_read(&f, buf, cap);
    (void)vfs_close(&f);
    return (int)n;
}

void ut_fs(void)
{
    static test_mfs_t mfs_a;
    static test_mfs_t mfs_b;
    vfs_file_t f;
    vfs_dir_t d;
    fs_dirent_t ent;
    fs_stat_t st;
    fs_statfs_t sfs;
    char buf[64];
    int n;

    memset(s_disk, 0xFF, sizeof(s_disk));
    memset(s_disk_b, 0xFF, sizeof(s_disk_b));

    tf_begin("fs/vfs-mount");
    TF_CHECK_INT(test_mfs_setup(&mfs_a, s_disk), FS_OK);
    TF_CHECK_INT(test_mfs_setup(&mfs_b, s_disk_b), FS_OK);
    TF_CHECK_INT(vfs_mount("/a", &s_test_ops, &mfs_a), FS_OK);
    TF_CHECK_INT(vfs_mount("/b", &s_test_ops, &mfs_b), FS_OK);
    TF_CHECK_INT(vfs_mount("/a", &s_test_ops, &mfs_a), FS_ERR_EXIST);
    TF_CHECK_INT(vfs_isdir("/a"), 1);
    TF_CHECK_INT(vfs_mkdir("/a/logs"), FS_OK);
    TF_CHECK_INT(vfs_mkdir("/a/logs"), FS_ERR_EXIST);
    TF_CHECK_INT(vfs_isdir("/a/logs"), 1);
    tf_end();

    tf_begin("fs/file-write-read");
    memset(&f, 0, sizeof(f));
    TF_CHECK_INT(vfs_open("/a/logs/a.txt",
                          FS_O_CREAT | FS_O_RDWR, &f), FS_OK);
    TF_CHECK_INT(vfs_write(&f, "hello mfs", 9), 9);
    TF_CHECK_INT(vfs_tell(&f), 9);
    TF_CHECK_INT(vfs_size(&f), 9);
    TF_CHECK_INT(vfs_seek(&f, 0, FS_SEEK_SET), FS_OK);
    memset(buf, 0, sizeof(buf));
    TF_CHECK_INT(vfs_read(&f, buf, 9), 9);
    TF_CHECK_STR(buf, "hello mfs");
    TF_CHECK_INT(vfs_close(&f), FS_OK);
    tf_end();

    tf_begin("fs/file-append");
    memset(&f, 0, sizeof(f));
    TF_CHECK_INT(vfs_open("/a/logs/a.txt", FS_O_WRONLY | FS_O_APPEND,
                          &f), FS_OK);
    TF_CHECK_INT(vfs_write(&f, "!", 1), 1);
    TF_CHECK_INT(vfs_close(&f), FS_OK);
    memset(buf, 0, sizeof(buf));
    n = read_all("/a/logs/a.txt", buf, sizeof(buf));
    TF_CHECK_INT(n, 10);
    TF_CHECK_STR(buf, "hello mfs!");
    tf_end();

    tf_begin("fs/rename-stat");
    TF_CHECK_INT(vfs_rename("/a/logs/a.txt", "/a/logs/b.txt"), FS_OK);
    TF_CHECK_INT(vfs_exists("/a/logs/a.txt"), 0);
    TF_CHECK_INT(vfs_exists("/a/logs/b.txt"), 1);
    TF_CHECK_INT(vfs_stat("/a/logs/b.txt", &st), FS_OK);
    TF_CHECK_STR(st.name, "b.txt");
    TF_CHECK_INT(st.type, FS_TYPE_FILE);
    TF_CHECK_INT(st.size, 10);
    tf_end();

    tf_begin("fs/readdir");
    memset(&d, 0, sizeof(d));
    TF_CHECK_INT(vfs_opendir("/a/logs", &d), FS_OK);
    n = 0;
    for (;;) {
        int rc = vfs_readdir(&d, &ent);
        if (rc == 1) {
            break;
        }
        TF_CHECK_INT(rc, 0);
        if (rc != 0) {
            break;
        }
        n++;
        TF_CHECK_STR(ent.name, "b.txt");
    }
    TF_CHECK_INT(n, 1);
    TF_CHECK_INT(vfs_closedir(&d), FS_OK);
    tf_end();

    tf_begin("fs/statfs");
    TF_CHECK_INT(vfs_statfs("/a", &sfs), FS_OK);
    TF_CHECK_INT(sfs.total_blocks, (int)(T_BLOCK_COUNT - 2u));
    TF_CHECK_INT(sfs.block_size, (int)T_BLOCK_SIZE);
    TF_CHECK(sfs.used_blocks > 0u);
    TF_CHECK(sfs.free_bytes < sfs.total_bytes);
    tf_end();

    tf_begin("fs/remount");
    TF_CHECK_INT(vfs_umount("/a"), FS_OK);
    TF_CHECK_INT(vfs_mount("/a", &s_test_ops, &mfs_a), FS_OK);
    memset(buf, 0, sizeof(buf));
    n = read_all("/a/logs/b.txt", buf, sizeof(buf));
    TF_CHECK_INT(n, 10);
    TF_CHECK_STR(buf, "hello mfs!");
    tf_end();

    tf_begin("fs/vfs-multi-fs");
    TF_CHECK_INT(vfs_mkdir("/b/dir"), FS_OK);
    memset(&f, 0, sizeof(f));
    TF_CHECK_INT(vfs_open("/b/dir/x.txt",
                          FS_O_CREAT | FS_O_RDWR, &f), FS_OK);
    TF_CHECK_INT(vfs_write(&f, "B", 1), 1);
    TF_CHECK_INT(vfs_close(&f), FS_OK);
    memset(buf, 0, sizeof(buf));
    n = read_all("/b/dir/x.txt", buf, sizeof(buf));
    TF_CHECK_INT(n, 1);
    TF_CHECK_STR(buf, "B");
    /* 两个挂载点互不影响 */
    TF_CHECK_INT(vfs_exists("/a/dir/x.txt"), 0);
    TF_CHECK_INT(vfs_rename("/b/dir/x.txt", "/a/x.txt"),
                 FS_ERR_NOTSUP);
    tf_end();

    tf_begin("fs/run-script");
    /* run：每个脚本一个动态任务；连续提交两个，验证并发与互不干扰 */
    {
        roshell_t *rs;
        rs_err_t err;

        if (!s_cmd_ready) {
            TF_CHECK_INT(fs_cmd_register(), 0);
            s_cmd_ready = true;
        }
        memset(&f, 0, sizeof(f));
        TF_CHECK_INT(vfs_open("/a/run1.txt",
                              FS_O_CREAT | FS_O_TRUNC | FS_O_WRONLY, &f), FS_OK);
        TF_CHECK_INT(vfs_write(&f, "put gen1.txt one\n", 17), 17);
        TF_CHECK_INT(vfs_close(&f), FS_OK);
        memset(&f, 0, sizeof(f));
        TF_CHECK_INT(vfs_open("/a/run2.txt",
                              FS_O_CREAT | FS_O_TRUNC | FS_O_WRONLY, &f), FS_OK);
        TF_CHECK_INT(vfs_write(&f, "put gen2.txt two\n", 17), 17);
        TF_CHECK_INT(vfs_close(&f), FS_OK);

        rs = rs_create();
        TF_CHECK(rs != NULL);
        if (rs != NULL) {
            int i;

            rs_cmd_install(rs);
            TF_CHECK_INT(rs_shell_cwd_set(rs, "/a"), 0);
            TF_CHECK_INT(rs_exec_line(rs, "run run1.txt", &err), RS_OK);
            /* 第二个脚本必须能同时启动（不再是单例） */
            TF_CHECK_INT(rs_exec_line(rs, "run run2.txt", &err), RS_OK);

            /* 执行任务与测试任务共享协作调度器：让出 CPU 等两个脚本都跑完 */
            for (i = 0; i < 200 &&
                        (vfs_exists("/a/gen1.txt") != 1 ||
                         vfs_exists("/a/gen2.txt") != 1); i++) {
                ros_sleep_ms(5);
            }
            TF_CHECK_INT(vfs_exists("/a/gen1.txt"), 1);
            TF_CHECK_INT(vfs_exists("/a/gen2.txt"), 1);
            memset(buf, 0, sizeof(buf));
            n = read_all("/a/gen1.txt", buf, sizeof(buf));
            TF_CHECK_INT(n, 3);
            TF_CHECK_STR(buf, "one");
            memset(buf, 0, sizeof(buf));
            n = read_all("/a/gen2.txt", buf, sizeof(buf));
            TF_CHECK_INT(n, 3);
            TF_CHECK_STR(buf, "two");
            rs_destroy(rs);
        }

        /* rs_exec_file：直接流式执行多行脚本（函数 + for + if），不整读文件 */
        {
            static const char s_loop_script[] =
                "function twice($a)\n"
                "return $a * 2\n"
                "end\n"
                "$i = 0\n"
                "for $i = 1,2,3 do\n"
                "$x = twice($i)\n"
                "end\n"
                "$v = 5\n"
                "if $v > 10\n"
                "$r = 1\n"
                "elseif $v > 3\n"
                "$r = 2\n"
                "else\n"
                "$r = 3\n"
                "end\n";

            memset(&f, 0, sizeof(f));
            TF_CHECK_INT(vfs_open("/a/loop.txt",
                                  FS_O_CREAT | FS_O_TRUNC | FS_O_WRONLY, &f),
                         FS_OK);
            TF_CHECK_INT(vfs_write(&f, s_loop_script,
                                   (uint32_t)(sizeof(s_loop_script) - 1U)),
                         (int32_t)(sizeof(s_loop_script) - 1U));
            TF_CHECK_INT(vfs_close(&f), FS_OK);
        }

        rs = rs_create();
        TF_CHECK(rs != NULL);
        if (rs != NULL) {
            int32_t v = 0;

            memset(&err, 0, sizeof(err));
            TF_CHECK_INT(rs_exec_file(rs, "/a/loop.txt", &err), RS_OK);
            TF_CHECK_INT(rs_get_int(rs, "x", &v), RS_OK);
            TF_CHECK_INT(v, 6);
            TF_CHECK_INT(rs_get_int(rs, "i", &v), RS_OK);
            TF_CHECK_INT(v, 3);
            TF_CHECK_INT(rs_get_int(rs, "r", &v), RS_OK);
            TF_CHECK_INT(v, 2);

            /* 文件不存在：返回 NOT_FOUND，不崩溃 */
            memset(&err, 0, sizeof(err));
            TF_CHECK_INT(rs_exec_file(rs, "/a/none.rs", &err), RS_ERR_NOT_FOUND);
            rs_destroy(rs);
        }
    }
    tf_end();

    tf_begin("fs/errors-remove");
    memset(&f, 0, sizeof(f));
    TF_CHECK_INT(vfs_open("/a/none.txt", FS_O_RDONLY, &f),
                 FS_ERR_NOENT);
    TF_CHECK_INT(vfs_unlink("/a/logs"), FS_ERR_ISDIR);
    TF_CHECK_INT(vfs_unlink("/a/logs/b.txt"), FS_OK);
    TF_CHECK_INT(vfs_exists("/a/logs/b.txt"), 0);
    TF_CHECK_INT(vfs_rmdir("/a/logs"), FS_OK);
    TF_CHECK_INT(vfs_exists("/a/logs"), 0);
    TF_CHECK_INT(vfs_stat("/other/x", &st), FS_ERR_NOENT);
    TF_CHECK_INT(vfs_umount("/nope"), FS_ERR_NOENT);
    TF_CHECK_INT(vfs_umount("/a"), FS_OK);
    TF_CHECK_INT(vfs_umount("/b"), FS_OK);
    tf_end();
}
