/*
 * app_fs.c - PC Shell 的 ROFS 文件镜像后端
 * ============================================================================
 *
 * 用一个普通文件模拟 SPI NOR：
 *   - 块大小 4096B，镜像 512KB，与 f407ve 的 ROFS 分区一致；
 *   - read   ：fseek + fread；
 *   - program：模拟 NOR “只能 1 -> 0”，逐块读旧值后与新数据按位与再写回；
 *   - erase  ：写入 0xFF；
 *   - sync   ：fflush。
 *
 * ROFS 通过 lock/unlock 串行化所有文件系统操作，本后端不再额外加锁。
 * 镜像缺失时创建，内容无有效 Superblock 时由 ROFS 自动格式化。
 */

#include "app_fs.h"

#include "ros.h"
#include "roshell.h"

#include <stdio.h>
#include <string.h>

#define IMG_SECTOR_SIZE   4096U
#define IMG_CHUNK_SIZE    512U   /* 模拟 NOR 读写时的临时块大小 */

static FILE *s_img;
static ros_mutex_t s_fs_lock;
static rofs_t s_rofs;
static uint8_t s_workbuf[ROFS_WORKBUF_SIZE];

/* ---------------- 文件镜像块设备 ---------------- */

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

/* ---------------- ROFS 适配 ---------------- */

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
 * 文件不足 APP_FS_IMG_SIZE 时扩展到最后 1 字节写入 0xFF；
 * 多余的旧内容由 ROFS 的格式化流程擦除。
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

/* ==========================================================================
 * Shell 命令：ls / cat / mkdir / rm / rmdir / stat / df / put
 * ========================================================================== */

/* 取文件系统句柄；未初始化时打印提示并返回 NULL */
static rofs_t *cmd_fs(void)
{
    rofs_t *fs = app_fs_handle();

    if (fs == NULL) {
        ros_puts("fs: not ready");
    }
    return fs;
}

/* ls [path]：列出目录内容，默认根目录 */
static int cmd_ls(const rs_cmd_ctx_t *ctx)
{
    rofs_t *fs = cmd_fs();
    rofs_dir_t *dir = NULL;
    rofs_dirent_t ent;
    const char *path = NULL;
    int rc;

    if (fs == NULL) {
        return -1;
    }
    (void)rs_args_get_str(ctx->parsed, 0U, &path);
    if (path == NULL || path[0] == '\0') {
        path = "/";
    }

    rc = rofs_opendir(fs, path, &dir);
    if (rc != ROFS_OK) {
        ros_printf("ls: %s: %s\n", path, rofs_strerror(rc));
        return -1;
    }

    for (;;) {
        rc = rofs_readdir(dir, &ent);
        if (rc == 1) {
            break;                       /* 遍历结束 */
        }
        if (rc < 0) {
            ros_printf("ls: %s\n", rofs_strerror(rc));
            break;
        }
        ros_printf("%c %8u  %s\n", (ent.type == 2U) ? 'd' : '-',
                   (unsigned)ent.size, ent.name);
    }

    (void)rofs_closedir(dir);
    return 0;
}

/* cat <path>：按原始字节输出文件内容 */
static int cmd_cat(const rs_cmd_ctx_t *ctx)
{
    rofs_t *fs = cmd_fs();
    rofs_file_t *f = NULL;
    const char *path = NULL;
    uint8_t buf[128];
    int n;
    int rc;

    if (fs == NULL) {
        return -1;
    }
    if (!rs_args_get_str(ctx->parsed, 0U, &path) || path == NULL) {
        ros_puts("usage: cat <path>");
        return -1;
    }

    rc = rofs_open(fs, path, ROFS_O_RDONLY, &f);
    if (rc != ROFS_OK) {
        ros_printf("cat: %s: %s\n", path, rofs_strerror(rc));
        return -1;
    }

    while ((n = rofs_read(f, buf, sizeof(buf))) > 0) {
        (void)ros_io_write(NULL, buf, (uint32_t)n);
    }
    if (n < 0) {
        ros_printf("\ncat: %s\n", rofs_strerror(n));
    }
    (void)rofs_close(f);
    return (n < 0) ? -1 : 0;
}

/* mkdir <path> */
static int cmd_mkdir(const rs_cmd_ctx_t *ctx)
{
    rofs_t *fs = cmd_fs();
    const char *path = NULL;
    int rc;

    if (fs == NULL) {
        return -1;
    }
    if (!rs_args_get_str(ctx->parsed, 0U, &path) || path == NULL) {
        ros_puts("usage: mkdir <path>");
        return -1;
    }
    rc = rofs_mkdir(fs, path);
    if (rc != ROFS_OK) {
        ros_printf("mkdir: %s: %s\n", path, rofs_strerror(rc));
        return -1;
    }
    return 0;
}

/* rm <path>：删除文件 */
static int cmd_rm(const rs_cmd_ctx_t *ctx)
{
    rofs_t *fs = cmd_fs();
    const char *path = NULL;
    int rc;

    if (fs == NULL) {
        return -1;
    }
    if (!rs_args_get_str(ctx->parsed, 0U, &path) || path == NULL) {
        ros_puts("usage: rm <path>");
        return -1;
    }
    rc = rofs_unlink(fs, path);
    if (rc == ROFS_ERR_ISDIR) {
        ros_printf("rm: %s: is a directory (use rmdir)\n", path);
        return -1;
    }
    if (rc != ROFS_OK) {
        ros_printf("rm: %s: %s\n", path, rofs_strerror(rc));
        return -1;
    }
    return 0;
}

/* rmdir <path>：删除空目录 */
static int cmd_rmdir(const rs_cmd_ctx_t *ctx)
{
    rofs_t *fs = cmd_fs();
    const char *path = NULL;
    int rc;

    if (fs == NULL) {
        return -1;
    }
    if (!rs_args_get_str(ctx->parsed, 0U, &path) || path == NULL) {
        ros_puts("usage: rmdir <path>");
        return -1;
    }
    rc = rofs_rmdir(fs, path);
    if (rc != ROFS_OK) {
        ros_printf("rmdir: %s: %s\n", path, rofs_strerror(rc));
        return -1;
    }
    return 0;
}

/* stat <path>：显示属性 */
static int cmd_stat(const rs_cmd_ctx_t *ctx)
{
    rofs_t *fs = cmd_fs();
    rofs_stat_t st;
    const char *path = NULL;
    int rc;

    if (fs == NULL) {
        return -1;
    }
    if (!rs_args_get_str(ctx->parsed, 0U, &path) || path == NULL) {
        ros_puts("usage: stat <path>");
        return -1;
    }
    rc = rofs_stat(fs, path, &st);
    if (rc != ROFS_OK) {
        ros_printf("stat: %s: %s\n", path, rofs_strerror(rc));
        return -1;
    }
    ros_printf("%s: inode=%u parent=%u type=%s size=%u version=%u\n",
               st.name, (unsigned)st.inode, (unsigned)st.parent,
               (st.type == 2U) ? "dir" : "file",
               (unsigned)st.size, (unsigned)st.version);
    return 0;
}

/* df：显示空间统计 */
static int cmd_df(const rs_cmd_ctx_t *ctx)
{
    rofs_t *fs = cmd_fs();
    rofs_statfs_t st;
    int rc;

    (void)ctx;
    if (fs == NULL) {
        return -1;
    }
    rc = rofs_statfs(fs, &st);
    if (rc != ROFS_OK) {
        ros_printf("df: %s\n", rofs_strerror(rc));
        return -1;
    }
    ros_printf("total=%uKB used=%uKB free=%uKB blocks=%u free_blocks=%u\n",
               (unsigned)(st.total_bytes / 1024U),
               (unsigned)(st.used_bytes / 1024U),
               (unsigned)(st.free_bytes / 1024U),
               (unsigned)st.total_blocks, (unsigned)st.free_blocks);
    return 0;
}

/* put <path> <text>：创建/覆盖文件（便于在 Shell 中验证读写） */
static int cmd_put(const rs_cmd_ctx_t *ctx)
{
    rofs_t *fs = cmd_fs();
    rofs_file_t *f = NULL;
    const char *path = NULL;
    const char *text = NULL;
    uint32_t len;
    int rc;

    if (fs == NULL) {
        return -1;
    }
    if (!rs_args_get_str(ctx->parsed, 0U, &path) ||
        !rs_args_get_str(ctx->parsed, 1U, &text) ||
        path == NULL || text == NULL) {
        ros_puts("usage: put <path> <text>");
        return -1;
    }

    len = (uint32_t)strlen(text);
    rc = rofs_open(fs, path, ROFS_O_CREAT | ROFS_O_TRUNC | ROFS_O_WRONLY, &f);
    if (rc != ROFS_OK) {
        ros_printf("put: %s: %s\n", path, rofs_strerror(rc));
        return -1;
    }
    rc = (int)rofs_write(f, text, len);
    (void)rofs_close(f);
    if (rc != (int)len) {
        ros_printf("put: %s: write failed\n", path);
        return -1;
    }
    ros_printf("put: %u bytes -> %s\n", (unsigned)len, path);
    return 0;
}

static const rs_args_t s_ls_args[] = {
    { "path", 0, RS_ARGS_TYPE_STR, true, false, "/", "directory (default /)" }
};

static const rs_args_t s_path_arg[] = {
    { "path", 0, RS_ARGS_TYPE_STR, true, true, NULL, "path" }
};

static const rs_args_t s_put_args[] = {
    { "path", 0, RS_ARGS_TYPE_STR, true, true, NULL, "file path" },
    { "text", 0, RS_ARGS_TYPE_STR, true, true, NULL, "text" }
};

static const rs_cmd_t s_ls_def = {
    "ls", "list ROFS directory [path]", NULL, NULL,
    s_ls_args, 1U, cmd_ls, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_cat_def = {
    "cat", "print file contents <path>", NULL, NULL,
    s_path_arg, 1U, cmd_cat, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_mkdir_def = {
    "mkdir", "create directory <path>", NULL, NULL,
    s_path_arg, 1U, cmd_mkdir, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_rm_def = {
    "rm", "remove file <path>", NULL, NULL,
    s_path_arg, 1U, cmd_rm, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_rmdir_def = {
    "rmdir", "remove empty directory <path>", NULL, NULL,
    s_path_arg, 1U, cmd_rmdir, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_stat_def = {
    "stat", "show ROFS entry attributes <path>", NULL, NULL,
    s_path_arg, 1U, cmd_stat, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_df_def = {
    "df", "show ROFS space usage", NULL, NULL,
    NULL, 0U, cmd_df, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_put_def = {
    "put", "create/overwrite file <path> <text>", NULL, NULL,
    s_put_args, 2U, cmd_put, NULL, NULL, NULL, NULL
};

/* 注册文件系统命令；在挂载成功后调用 */
static void app_fs_register_cmds(void)
{
    (void)rs_cmd_register(&s_ls_def);
    (void)rs_cmd_register(&s_cat_def);
    (void)rs_cmd_register(&s_mkdir_def);
    (void)rs_cmd_register(&s_rm_def);
    (void)rs_cmd_register(&s_rmdir_def);
    (void)rs_cmd_register(&s_stat_def);
    (void)rs_cmd_register(&s_df_def);
    (void)rs_cmd_register(&s_put_def);
}

int app_fs_init(const char *img_path)
{
    rofs_blockdev_t dev;
    rofs_config_t cfg;
    rofs_statfs_t stat;
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

    memset(&dev, 0, sizeof(dev));
    dev.read = img_read;
    dev.program = img_program;
    dev.erase = img_erase;
    dev.sync = img_sync;
    dev.start_addr = 0u;
    dev.total_size = APP_FS_IMG_SIZE;
    dev.erase_size = IMG_SECTOR_SIZE;
    dev.program_size = 1u;
    dev.read_size = 1u;

    memset(&cfg, 0, sizeof(cfg));
    cfg.workbuf = s_workbuf;
    cfg.workbuf_size = sizeof(s_workbuf);
    cfg.max_open_files = ROFS_MAX_OPEN_FILES;
    cfg.max_open_dirs = ROFS_MAX_OPEN_DIRS;
    cfg.lock = fs_lock;
    cfg.unlock = fs_unlock;

    rc = rofs_init(&s_rofs, &dev, &cfg);
    if (rc != ROFS_OK) {
        ros_printf("fs: rofs init failed (%d)\n", rc);
        (void)app_fs_deinit();
        return -1;
    }

    /* 镜像首次使用或 Superblock 损坏时自动格式化 */
    rc = rofs_mount(&s_rofs);
    if (rc != ROFS_OK) {
        ros_printf("fs: mount failed (%d), formatting %s\n", rc, img_path);
        rc = rofs_format(&s_rofs);
    }
    if (rc != ROFS_OK) {
        ros_printf("fs: format failed (%d)\n", rc);
        (void)app_fs_deinit();
        return -1;
    }

    if (rofs_statfs(&s_rofs, &stat) == ROFS_OK) {
        ros_printf("fs: %s mounted, %u/%u KB free\n", img_path,
                   (unsigned)(stat.free_bytes / 1024U),
                   (unsigned)(stat.total_bytes / 1024U));
    } else {
        ros_printf("fs: %s mounted\n", img_path);
    }

    app_fs_register_cmds();
    return 0;
}

void app_fs_deinit(void)
{
    if (s_img == NULL) {
        return;
    }
    (void)rofs_unmount(&s_rofs);
    (void)fclose(s_img);
    s_img = NULL;
}

rofs_t *app_fs_handle(void)
{
    return (s_img != NULL) ? &s_rofs : NULL;
}
