/*
 * fs_cmd.c - 通用文件系统 Shell 命令（基于 VFS）
 * ============================================================================
 *
 * 命令：pwd / cd / ls / cat / mkdir / rmdir / rm / stat / df / put / format
 * 不直接依赖具体后端，挂载点由 VFS 前缀分发决定。
 *
 * 当前目录（cwd）保存在 roshell 会话中，默认 "/"。
 * 除 format 外，所有路径参数都支持相对路径（相对 cwd），
 * 并会规范化 "." / ".." 与重复斜杠后再交给 VFS。
 */

#include "fs_cmd.h"

#include "ros.h"
#include "roshell.h"
#include "vfs.h"

#include <string.h>

/* ls 递归深度与单层最多下探的子目录数（限制栈占用） */
#define LS_MAX_DEPTH   2U
#define LS_MAX_CHILD   4U

/*
 * 把输入路径解析为绝对路径并规范化。
 * in 为空时取 cwd；相对路径拼接到 cwd 之后。
 * out 保证以 '/' 开头，形如 "/"、"/a"、"/a/b"。
 */
static void fs_path_abs(const roshell_t *rs, const char *in, char *out, uint32_t cap)
{
    const char *p;
    uint32_t n = 0U;

    if (out == NULL || cap < 2U) {
        return;
    }
    out[0] = '\0';

    if (in == NULL || in[0] == '\0') {
        in = ".";
    }

    if (in[0] != '/') {
        const char *cwd = rs_shell_cwd(rs);

        while (*cwd != '\0' && n + 1U < cap) {
            out[n++] = *cwd++;
        }
        out[n] = '\0';
    }

    p = in;
    while (*p != '\0') {
        const char *seg;
        uint32_t len;

        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        seg = p;
        while (*p != '\0' && *p != '/') {
            p++;
        }
        len = (uint32_t)(p - seg);

        if (len == 1U && seg[0] == '.') {
            continue;
        }
        if (len == 2U && seg[0] == '.' && seg[1] == '.') {
            while (n > 0U && out[n - 1U] != '/') {
                n--;
            }
            if (n > 0U) {
                n--;
            }
            out[n] = '\0';
            continue;
        }
        if (n + len + 2U > cap) {
            break;                          /* 超长：忽略剩余部分 */
        }
        if (n == 0U || out[n - 1U] != '/') {
            out[n++] = '/';
        }
        memcpy(&out[n], seg, len);
        n += len;
        out[n] = '\0';
    }

    if (n == 0U) {
        out[0] = '/';
        out[1] = '\0';
    }
}

/* 拼接目录与名称；超长返回 -1 */
static int fs_path_join(const char *dir, const char *name, char *out,
                        uint32_t cap)
{
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    uint32_t n;

    if (dlen + nlen + 2U > cap) {
        return -1;
    }
    memcpy(out, dir, dlen);
    n = (uint32_t)dlen;
    if (n == 0U || out[n - 1U] != '/') {
        out[n++] = '/';
    }
    memcpy(&out[n], name, nlen);
    n += (uint32_t)nlen;
    out[n] = '\0';
    return 0;
}

/* pwd：显示当前目录 */
static int cmd_pwd(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    (void)argc;
    (void)argv;
    (void)user;
    ros_printf("%s\n", rs_shell_cwd(rs));
    return 0;
}

/* cd [path]：切换当前目录，缺省回根目录 */
static int cmd_cd(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    (void)argc;
    (void)argv;
    (void)user;
    const char *path = NULL;
    char abs[RS_CWD_MAX];
    int rc;

    (void)rs_args_get_str(parsed, 0U, &path);
    if (path == NULL || path[0] == '\0') {
        path = "/";
    }
    fs_path_abs(rs, path, abs, sizeof(abs));

    rc = vfs_isdir(abs);
    if (rc != 1) {
        ros_printf("cd: %s: %s\n", abs,
                   (rc == 0) ? "not a directory" : fs_strerror(rc));
        return -1;
    }
    if (rs_shell_cwd_set(rs, abs) != 0) {
        ros_puts("cd: path too long");
        return -1;
    }
    return 0;
}

/* 递归列出目录内容 */
static void ls_dir(const char *path, uint32_t depth)
{
    vfs_dir_t dir;
    fs_dirent_t ent;
    char child[LS_MAX_CHILD][FS_NAME_MAX + 1];
    uint32_t count = 0U;
    uint32_t i;
    int rc;

    ros_printf("%s:\n", path);
    memset(&dir, 0, sizeof(dir));
    rc = vfs_opendir(path, &dir);
    if (rc != FS_OK) {
        ros_printf("ls: %s: %s\n", path, fs_strerror(rc));
        return;
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
        if (ent.type == FS_TYPE_DIR && count < LS_MAX_CHILD) {
            (void)strncpy(child[count], ent.name, FS_NAME_MAX);
            child[count][FS_NAME_MAX] = '\0';
            count++;
        }
    }
    (void)vfs_closedir(&dir);

    if (depth >= LS_MAX_DEPTH) {
        return;
    }
    for (i = 0U; i < count; i++) {
        char sub[RS_CWD_MAX];

        if (fs_path_join(path, child[i], sub, sizeof(sub)) == 0) {
            ls_dir(sub, depth + 1U);
        }
    }
}

/* ls [path]：列出当前目录（或指定目录）及子目录内容 */
static int cmd_ls(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    (void)argc;
    (void)argv;
    (void)user;
    char abs[RS_CWD_MAX];
    const char *path = NULL;

    (void)rs_args_get_str(parsed, 0U, &path);
    fs_path_abs(rs, path, abs, sizeof(abs));
    ls_dir(abs, 0U);
    return 0;
}

/* cat <path>：按原始字节输出文件内容 */
static int cmd_cat(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    (void)argc;
    (void)argv;
    (void)user;
    vfs_file_t f;
    const char *path = NULL;
    char abs[RS_CWD_MAX];
    uint8_t buf[128];
    int n;
    int rc;

    if (!rs_args_get_str(parsed, 0U, &path) || path == NULL) {
        ros_puts("usage: cat <path>");
        return -1;
    }
    fs_path_abs(rs, path, abs, sizeof(abs));

    memset(&f, 0, sizeof(f));
    rc = vfs_open(abs, FS_O_RDONLY, &f);
    if (rc != FS_OK) {
        ros_printf("cat: %s: %s\n", abs, fs_strerror(rc));
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
static int cmd_mkdir(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    (void)argc;
    (void)argv;
    (void)user;
    const char *path = NULL;
    char abs[RS_CWD_MAX];
    int rc;

    if (!rs_args_get_str(parsed, 0U, &path) || path == NULL) {
        ros_puts("usage: mkdir <path>");
        return -1;
    }
    fs_path_abs(rs, path, abs, sizeof(abs));
    rc = vfs_mkdir(abs);
    if (rc != FS_OK) {
        ros_printf("mkdir: %s: %s\n", abs, fs_strerror(rc));
        return -1;
    }
    return 0;
}

/* rm <path>：删除文件 */
static int cmd_rm(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    (void)argc;
    (void)argv;
    (void)user;
    const char *path = NULL;
    char abs[RS_CWD_MAX];
    int rc;

    if (!rs_args_get_str(parsed, 0U, &path) || path == NULL) {
        ros_puts("usage: rm <path>");
        return -1;
    }
    fs_path_abs(rs, path, abs, sizeof(abs));
    rc = vfs_unlink(abs);
    if (rc == FS_ERR_ISDIR) {
        ros_printf("rm: %s: is a directory (use rmdir)\n", abs);
        return -1;
    }
    if (rc != FS_OK) {
        ros_printf("rm: %s: %s\n", abs, fs_strerror(rc));
        return -1;
    }
    return 0;
}

/* rmdir <path>：删除空目录 */
static int cmd_rmdir(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    (void)argc;
    (void)argv;
    (void)user;
    const char *path = NULL;
    char abs[RS_CWD_MAX];
    int rc;

    if (!rs_args_get_str(parsed, 0U, &path) || path == NULL) {
        ros_puts("usage: rmdir <path>");
        return -1;
    }
    fs_path_abs(rs, path, abs, sizeof(abs));
    rc = vfs_rmdir(abs);
    if (rc != FS_OK) {
        ros_printf("rmdir: %s: %s\n", abs, fs_strerror(rc));
        return -1;
    }
    return 0;
}

/* stat <path>：显示属性 */
static int cmd_stat(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    (void)argc;
    (void)argv;
    (void)user;
    fs_stat_t st;
    const char *path = NULL;
    char abs[RS_CWD_MAX];
    int rc;

    if (!rs_args_get_str(parsed, 0U, &path) || path == NULL) {
        ros_puts("usage: stat <path>");
        return -1;
    }
    fs_path_abs(rs, path, abs, sizeof(abs));
    rc = vfs_stat(abs, &st);
    if (rc != FS_OK) {
        ros_printf("stat: %s: %s\n", abs, fs_strerror(rc));
        return -1;
    }
    ros_printf("%s: type=%s size=%u\n", st.name,
               (st.type == FS_TYPE_DIR) ? "dir" : "file",
               (unsigned)st.size);
    return 0;
}

/* df [path]：显示空间统计，默认当前目录 */
static int cmd_df(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    (void)argc;
    (void)argv;
    (void)user;
    fs_statfs_t st;
    const char *path = NULL;
    char abs[RS_CWD_MAX];
    int rc;

    (void)rs_args_get_str(parsed, 0U, &path);
    fs_path_abs(rs, path, abs, sizeof(abs));
    rc = vfs_statfs(abs, &st);
    if (rc != FS_OK) {
        ros_printf("df: %s: %s\n", abs, fs_strerror(rc));
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
static int cmd_put(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    (void)argc;
    (void)argv;
    (void)user;
    vfs_file_t f;
    const char *path = NULL;
    const char *text = NULL;
    char abs[RS_CWD_MAX];
    uint32_t len;
    int rc;

    if (!rs_args_get_str(parsed, 0U, &path) ||
        !rs_args_get_str(parsed, 1U, &text) ||
        path == NULL || text == NULL) {
        ros_puts("usage: put <path> <text>");
        return -1;
    }
    fs_path_abs(rs, path, abs, sizeof(abs));

    len = (uint32_t)strlen(text);
    memset(&f, 0, sizeof(f));
    rc = vfs_open(abs, FS_O_CREAT | FS_O_TRUNC | FS_O_WRONLY, &f);
    if (rc != FS_OK) {
        ros_printf("put: %s: %s\n", abs, fs_strerror(rc));
        return -1;
    }
    rc = (int)vfs_write(&f, text, len);
    (void)vfs_close(&f);
    if (rc != (int)len) {
        ros_printf("put: %s: write failed\n", abs);
        return -1;
    }
    ros_printf("put: %u bytes -> %s\n", (unsigned)len, abs);
    return 0;
}

/* format [path]：格式化挂载点（清空数据），默认根挂载点 */
static int cmd_format(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    (void)argc;
    (void)argv;
    (void)user;
    const char *path = NULL;
    int rc;

    (void)rs_args_get_str(parsed, 0U, &path);
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
    { "path", 0, RS_ARGS_TYPE_STR, true, false, NULL, "directory (default cwd)" }
};

static const rs_args_t s_df_args[] = {
    { "path", 0, RS_ARGS_TYPE_STR, true, false, NULL, "path (default cwd)" }
};

static const rs_args_t s_cd_args[] = {
    { "path", 0, RS_ARGS_TYPE_STR, true, false, "/", "target directory" }
};

static const rs_args_t s_path_arg[] = {
    { "path", 0, RS_ARGS_TYPE_STR, true, true, NULL, "path" }
};

static const rs_args_t s_put_args[] = {
    { "path", 0, RS_ARGS_TYPE_STR, true, true, NULL, "file path" },
    { "text", 0, RS_ARGS_TYPE_STR, true, true, NULL, "text" }
};

static const rs_cmd_t s_pwd_def = {
    "pwd", "print working directory", NULL, NULL,
    NULL, 0U, cmd_pwd, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_cd_def = {
    "cd", "change working directory [path]", NULL, NULL,
    s_cd_args, 1U, cmd_cd, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_ls_def = {
    "ls", "list directory recursively [path]", NULL, NULL,
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

    rc |= rs_cmd_register(&s_pwd_def);
    rc |= rs_cmd_register(&s_cd_def);
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
