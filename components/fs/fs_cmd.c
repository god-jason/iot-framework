/*
 * fs_cmd.c - 通用文件系统 Shell 命令（基于 VFS）
 * ============================================================================
 *
 * 命令：ls / cat / mkdir / rmdir / rm / stat / df / put / format
 * 不直接依赖具体后端，挂载点由 VFS 前缀分发决定。
 */

#include "fs_cmd.h"

#include "ros.h"
#include "roshell.h"
#include "vfs.h"

#include <string.h>

/* ls [path]：列出目录内容，默认根目录 */
static int cmd_ls(const rs_cmd_ctx_t *ctx)
{
    vfs_dir_t dir;
    fs_dirent_t ent;
    const char *path = NULL;
    int rc;

    (void)rs_args_get_str(ctx->parsed, 0U, &path);
    if (path == NULL || path[0] == '\0') {
        path = "/";
    }

    memset(&dir, 0, sizeof(dir));
    rc = vfs_opendir(path, &dir);
    if (rc != FS_OK) {
        ros_printf("ls: %s: %s\n", path, fs_strerror(rc));
        return -1;
    }

    for (;;) {
        rc = vfs_readdir(&dir, &ent);
        if (rc == 1) {
            break;                       /* 遍历结束 */
        }
        if (rc < 0) {
            ros_printf("ls: %s\n", fs_strerror(rc));
            break;
        }
        ros_printf("%c %8u  %s\n", (ent.type == FS_TYPE_DIR) ? 'd' : '-',
                   (unsigned)ent.size, ent.name);
    }

    (void)vfs_closedir(&dir);
    return 0;
}

/* cat <path>：按原始字节输出文件内容 */
static int cmd_cat(const rs_cmd_ctx_t *ctx)
{
    vfs_file_t f;
    const char *path = NULL;
    uint8_t buf[128];
    int n;
    int rc;

    if (!rs_args_get_str(ctx->parsed, 0U, &path) || path == NULL) {
        ros_puts("usage: cat <path>");
        return -1;
    }

    memset(&f, 0, sizeof(f));
    rc = vfs_open(path, FS_O_RDONLY, &f);
    if (rc != FS_OK) {
        ros_printf("cat: %s: %s\n", path, fs_strerror(rc));
        return -1;
    }

    while ((n = vfs_read(&f, buf, sizeof(buf))) > 0) {
        (void)ros_io_write(NULL, buf, (uint32_t)n);
    }
    if (n < 0) {
        ros_printf("\ncat: %s\n", fs_strerror(n));
    }
    (void)vfs_close(&f);
    return (n < 0) ? -1 : 0;
}

/* mkdir <path> */
static int cmd_mkdir(const rs_cmd_ctx_t *ctx)
{
    const char *path = NULL;
    int rc;

    if (!rs_args_get_str(ctx->parsed, 0U, &path) || path == NULL) {
        ros_puts("usage: mkdir <path>");
        return -1;
    }
    rc = vfs_mkdir(path);
    if (rc != FS_OK) {
        ros_printf("mkdir: %s: %s\n", path, fs_strerror(rc));
        return -1;
    }
    return 0;
}

/* rm <path>：删除文件 */
static int cmd_rm(const rs_cmd_ctx_t *ctx)
{
    const char *path = NULL;
    int rc;

    if (!rs_args_get_str(ctx->parsed, 0U, &path) || path == NULL) {
        ros_puts("usage: rm <path>");
        return -1;
    }
    rc = vfs_unlink(path);
    if (rc == FS_ERR_ISDIR) {
        ros_printf("rm: %s: is a directory (use rmdir)\n", path);
        return -1;
    }
    if (rc != FS_OK) {
        ros_printf("rm: %s: %s\n", path, fs_strerror(rc));
        return -1;
    }
    return 0;
}

/* rmdir <path>：删除空目录 */
static int cmd_rmdir(const rs_cmd_ctx_t *ctx)
{
    const char *path = NULL;
    int rc;

    if (!rs_args_get_str(ctx->parsed, 0U, &path) || path == NULL) {
        ros_puts("usage: rmdir <path>");
        return -1;
    }
    rc = vfs_rmdir(path);
    if (rc != FS_OK) {
        ros_printf("rmdir: %s: %s\n", path, fs_strerror(rc));
        return -1;
    }
    return 0;
}

/* stat <path>：显示属性 */
static int cmd_stat(const rs_cmd_ctx_t *ctx)
{
    fs_stat_t st;
    const char *path = NULL;
    int rc;

    if (!rs_args_get_str(ctx->parsed, 0U, &path) || path == NULL) {
        ros_puts("usage: stat <path>");
        return -1;
    }
    rc = vfs_stat(path, &st);
    if (rc != FS_OK) {
        ros_printf("stat: %s: %s\n", path, fs_strerror(rc));
        return -1;
    }
    ros_printf("%s: type=%s size=%u\n", st.name,
               (st.type == FS_TYPE_DIR) ? "dir" : "file",
               (unsigned)st.size);
    return 0;
}

/* df [path]：显示空间统计，默认根目录 */
static int cmd_df(const rs_cmd_ctx_t *ctx)
{
    fs_statfs_t st;
    const char *path = NULL;
    int rc;

    (void)rs_args_get_str(ctx->parsed, 0U, &path);
    if (path == NULL || path[0] == '\0') {
        path = "/";
    }
    rc = vfs_statfs(path, &st);
    if (rc != FS_OK) {
        ros_printf("df: %s: %s\n", path, fs_strerror(rc));
        return -1;
    }
    ros_printf("total=%uKB used=%uKB free=%uKB blocks=%u used_blocks=%u\n",
               (unsigned)(st.total_bytes / 1024U),
               (unsigned)(st.used_bytes / 1024U),
               (unsigned)(st.free_bytes / 1024U),
               (unsigned)st.total_blocks, (unsigned)st.used_blocks);
    return 0;
}

/* put <path> <text>：创建/覆盖文件（便于验证读写） */
static int cmd_put(const rs_cmd_ctx_t *ctx)
{
    vfs_file_t f;
    const char *path = NULL;
    const char *text = NULL;
    uint32_t len;
    int rc;

    if (!rs_args_get_str(ctx->parsed, 0U, &path) ||
        !rs_args_get_str(ctx->parsed, 1U, &text) ||
        path == NULL || text == NULL) {
        ros_puts("usage: put <path> <text>");
        return -1;
    }

    len = (uint32_t)strlen(text);
    memset(&f, 0, sizeof(f));
    rc = vfs_open(path, FS_O_CREAT | FS_O_TRUNC | FS_O_WRONLY, &f);
    if (rc != FS_OK) {
        ros_printf("put: %s: %s\n", path, fs_strerror(rc));
        return -1;
    }
    rc = (int)vfs_write(&f, text, len);
    (void)vfs_close(&f);
    if (rc != (int)len) {
        ros_printf("put: %s: write failed\n", path);
        return -1;
    }
    ros_printf("put: %u bytes -> %s\n", (unsigned)len, path);
    return 0;
}

/* format [path]：格式化挂载点（清空数据），默认根挂载点 */
static int cmd_format(const rs_cmd_ctx_t *ctx)
{
    const char *path = NULL;
    int rc;

    (void)rs_args_get_str(ctx->parsed, 0U, &path);
    if (path == NULL || path[0] == '\0') {
        path = "/";
    }
    rc = vfs_format(path);
    if (rc != FS_OK) {
        ros_printf("format: %s: %s\n", path, fs_strerror(rc));
        return -1;
    }
    ros_printf("format: %s done\n", path);
    return 0;
}

static const rs_args_t s_ls_args[] = {
    { "path", 0, RS_ARGS_TYPE_STR, true, false, "/", "directory (default /)" }
};

static const rs_args_t s_df_args[] = {
    { "path", 0, RS_ARGS_TYPE_STR, true, false, "/", "mount point (default /)" }
};

static const rs_args_t s_path_arg[] = {
    { "path", 0, RS_ARGS_TYPE_STR, true, true, NULL, "path" }
};

static const rs_args_t s_put_args[] = {
    { "path", 0, RS_ARGS_TYPE_STR, true, true, NULL, "file path" },
    { "text", 0, RS_ARGS_TYPE_STR, true, true, NULL, "text" }
};

static const rs_cmd_t s_ls_def = {
    "ls", "list directory [path]", NULL, NULL,
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
    "stat", "show entry attributes <path>", NULL, NULL,
    s_path_arg, 1U, cmd_stat, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_df_def = {
    "df", "show file system space usage [path]", NULL, NULL,
    s_df_args, 1U, cmd_df, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_put_def = {
    "put", "create/overwrite file <path> <text>", NULL, NULL,
    s_put_args, 2U, cmd_put, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_format_def = {
    "format", "format mount point [path]", NULL, NULL,
    s_df_args, 1U, cmd_format, NULL, NULL, NULL, NULL
};

int fs_cmd_register(void)
{
    int rc = 0;

    rc |= rs_cmd_register(&s_ls_def);
    rc |= rs_cmd_register(&s_cat_def);
    rc |= rs_cmd_register(&s_mkdir_def);
    rc |= rs_cmd_register(&s_rm_def);
    rc |= rs_cmd_register(&s_rmdir_def);
    rc |= rs_cmd_register(&s_stat_def);
    rc |= rs_cmd_register(&s_df_def);
    rc |= rs_cmd_register(&s_put_def);
    rc |= rs_cmd_register(&s_format_def);
    return (rc == 0) ? 0 : -1;
}
