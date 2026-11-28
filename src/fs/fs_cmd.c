/*
 * fs_cmd.c - 通用文件系统 Shell 命令（基于 VFS）
 * ============================================================================
 *
 * 命令：pwd / cd / ls / cat / mkdir / rmdir / rm / stat / df / put / run / format
 * 不直接依赖具体后端，挂载点由 VFS 前缀分发决定。
 * run 从文件系统加载脚本，在独立任务中执行（见下方 run 段落）。
 *
 * 当前目录（cwd）保存在 roshell 会话中，默认 "/"。
 * 除 format 外，所有路径参数都支持相对路径（相对 cwd），
 * 并会规范化 "." / ".." 与重复斜杠后再交给 VFS。
 */

#include "fs_cmd.h"

#include "ros.h"
#include "roshell.h"
#include "roshell_repl.h"
#include "roshell_fs.h"
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

/*
 * put <path> <text>...：创建/覆盖文件。
 *
 * 文本由第 2 个实参起的所有词组成，词间补一个空格——否则无法生成
 * 带空格的内容，而脚本行（如 "sleep 500"、"ls /a"）几乎都需要空格。
 * 因为要接收可变数量的实参，这里不使用参数表（parsed 为 NULL），
 * 改为直接读 argv，避免多余实参被判为 RS_ARGS_ERR_EXTRA。
 */
static int cmd_put(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    (void)parsed;
    (void)user;
    vfs_file_t f;
    char abs[RS_CWD_MAX];
    uint32_t written = 0U;
    int i;
    int rc;

    if (argc < 3 || argv == NULL || argv[1] == NULL) {
        ros_puts("usage: put <path> <text>...");
        return -1;
    }
    fs_path_abs(rs, argv[1], abs, sizeof(abs));

    memset(&f, 0, sizeof(f));
    rc = vfs_open(abs, FS_O_CREAT | FS_O_TRUNC | FS_O_WRONLY, &f);
    if (rc != FS_OK) {
        ros_printf("put: %s: %s\n", abs, fs_strerror(rc));
        return -1;
    }
    for (i = 2; i < argc; i++) {
        uint32_t len;
        int32_t n;

        if (i > 2) {
            if (vfs_write(&f, " ", 1U) != 1) {
                goto write_fail;
            }
            written++;
        }
        len = (uint32_t)strlen(argv[i]);
        n = vfs_write(&f, argv[i], len);
        if (n != (int32_t)len) {
            goto write_fail;
        }
        written += len;
    }
    (void)vfs_close(&f);
    ros_printf("put: %u bytes -> %s\n", (unsigned)written, abs);
    return 0;

write_fail:
    (void)vfs_close(&f);
    ros_printf("put: %s: write failed\n", abs);
    return -1;
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

/*
 * ================================ run <path> ================================
 * 从文件系统加载脚本，在独立任务里交给 roshell 执行。
 *
 * 设计要点：
 *   · 命令只做校验与投递，立即返回，交互 shell 不会被长脚本阻塞；
 *   · 每个脚本一个动态任务（ros_task_create_dynamic）：TCB 与栈都来自内核堆，
 *     同时运行的脚本数量不受静态槽位限制，只受剩余堆大小约束；
 *   · 任务不能销毁自己：结束（退出）后由其它任务在下次提交时回收 TCB/栈；
 *   · 脚本不整读：交给 roshell_fs 的 rs_exec_file() 流式执行（行索引 +
 *     按行取源），堆开销只有 4B/行 的行偏移表，与文件字节数无关；
 *   · 行数上限 RS_LINE_SRC_MAX（默认 256），单行长度上限 RS_LINE_MAX。
 */
#ifndef FS_RUN_TASK_STACK
#define FS_RUN_TASK_STACK   RS_SHELL_STACK  /* 每个执行任务的栈大小（动态分配） */
#endif

#ifndef FS_RUN_TASK_PRIO
#define FS_RUN_TASK_PRIO    RS_SHELL_PRIO   /* 与交互任务同优先级，避免抢占输入 */
#endif

/* 一次脚本执行：任务句柄 + 路径/工作目录，整体动态分配，退出后回收 */
typedef struct fs_run_job {
    struct fs_run_job *next;                /* 运行中任务链表 */
    ros_task_t        *task;                /* 动态创建的任务 */
    char               path[RS_CWD_MAX];    /* 脚本绝对路径 */
    char               cwd[RS_CWD_MAX];     /* 脚本工作目录（继承发起会话） */
    uint32_t           size;                /* 已校验的字节数（作为源大小） */
} fs_run_job_t;

static fs_run_job_t *s_run_jobs;            /* 正在执行的脚本（s_run_lock 保护） */
static ros_mutex_t   s_run_lock;
static bool          s_run_ready;

/* 执行任务主体：流式执行脚本文件（打开/读/关都在 roshell_fs 内） */
static void fs_run_task_entry(void *arg)
{
    fs_run_job_t *job = (fs_run_job_t *)arg;
    roshell_t *rs;
    rs_err_t err;
    int rc;

    rs = rs_create();
    if (rs == NULL) {
        ros_printf("run: %s: no memory for shell context\n", job->path);
        goto out;
    }
    rs_cmd_install(rs);                     /* 挂上全局命令表，脚本内可用所有命令 */
    (void)rs_shell_cwd_set(rs, job->cwd);

    memset(&err, 0, sizeof(err));
    rc = rs_exec_file(rs, job->path, &err);
    if (rc != RS_OK) {
        ros_printf("run: %s:%u: %s\n", job->path,
                   (unsigned)((err.line != 0U) ? err.line : 1U),
                   (err.msg[0] != '\0') ? err.msg : "exec failed");
    } else {
        ros_printf("run: %s: done\n", job->path);
    }
    rs_destroy(rs);

out:
    /* 任务不能销毁自己：退出后 TCB/栈由回收路径统一释放 */
    ros_task_exit();
}

/* 懒初始化运行链表锁 */
static int fs_run_init(void)
{
    int rc;

    if (s_run_ready) {
        return 0;
    }
    rc = ros_mutex_init(&s_run_lock);
    if (rc != ROS_OK) {
        ros_printf("run: mutex init failed (%d)\n", rc);
        return -1;
    }
    s_run_ready = true;
    return 0;
}

/*
 * 回收已退出的脚本任务。
 *
 * 任务不能销毁自己（ros_task_destroy 对当前任务返回 ROS_BUSY），所以执行任务
 * 结束时只调用 ros_task_exit()，TCB 与任务栈仍留在堆上；这里把已处于
 * TERMINATED 的项摘下来，解锁后再 destroy（释放 TCB + 栈）并释放上下文。
 * 这样做而不是在锁内销毁，避免长时间关中断/持锁。
 */
static void fs_run_reap(void)
{
    fs_run_job_t *dead = NULL;
    fs_run_job_t **pp;
    fs_run_job_t *job;

    if (!s_run_ready) {
        return;
    }
    if (ros_mutex_lock(&s_run_lock, ROS_TIMEOUT_NONE) != ROS_OK) {
        return;                             /* 有其它调用在改链表，下次再收 */
    }
    pp = &s_run_jobs;
    while (*pp != NULL) {
        job = *pp;
        if (job->task != NULL && job->task->state == ROS_TASK_TERMINATED) {
            *pp = job->next;
            job->next = dead;
            dead = job;
            continue;
        }
        pp = &job->next;
    }
    (void)ros_mutex_unlock(&s_run_lock);

    while (dead != NULL) {
        job = dead;
        dead = dead->next;
        (void)ros_task_destroy(job->task);  /* 释放动态 TCB 与任务栈 */
        ros_mem_free(job);
    }
}

/*
 * 为脚本创建独立任务并投递（调用者已校验过路径与长度）。
 * 并发数量不设静态上限：每份任务与上下文都来自内核堆，失败即报内存不足。
 */
static int fs_run_submit(const roshell_t *rs, const char *abs, uint32_t size)
{
    fs_run_job_t *job;
    const char *cwd;
    size_t len;

    if (fs_run_init() != 0) {
        return -1;
    }
    fs_run_reap();                          /* 先回收已结束的任务，腾出堆空间 */

    job = (fs_run_job_t *)ros_mem_calloc(1U, sizeof(*job));
    if (job == NULL) {
        ros_puts("run: out of memory for job context");
        return -1;
    }

    len = strlen(abs);
    if (len >= sizeof(job->path)) {
        len = sizeof(job->path) - 1U;
    }
    memcpy(job->path, abs, len);
    job->path[len] = '\0';

    cwd = rs_shell_cwd(rs);
    len = strlen(cwd);
    if (len >= sizeof(job->cwd)) {
        len = sizeof(job->cwd) - 1U;
    }
    memcpy(job->cwd, cwd, len);
    job->cwd[len] = '\0';
    job->size = size;

    job->task = ros_task_create_dynamic("fs_run", (uint8_t)FS_RUN_TASK_PRIO,
                                        fs_run_task_entry, job,
                                        (uint32_t)FS_RUN_TASK_STACK);
    if (job->task == NULL) {
        ros_mem_free(job);
        ros_printf("run: %s: task create failed (out of memory)\n", abs);
        return -1;
    }

    /* 挂入运行链表：任务已可能开始执行，但只有退出后才会被回收 */
    if (ros_mutex_lock(&s_run_lock, ROS_TIMEOUT_FOREVER) == ROS_OK) {
        job->next = s_run_jobs;
        s_run_jobs = job;
        (void)ros_mutex_unlock(&s_run_lock);
    }

    ros_printf("run: %s: started (%u bytes)\n", job->path, (unsigned)size);

    /*
     * 主动让出 CPU：执行任务与交互任务同优先级，在协作式/单核无抢占的
     * 移植层（如 PC 模拟）里，若不让出，新建的执行任务会一直停在 READY，
     * 表现为"started 之后没有下文"。让出后它立刻开始跑，脚本自己阻塞
     * （sleep/等 IO）时再回到交互循环。
     */
    ros_yield();
    return 0;
}

/* run <path>：校验脚本并创建动态任务执行（立即返回） */
static int cmd_run(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    (void)argc;
    (void)argv;
    (void)user;

    const char *path = NULL;
    char abs[RS_CWD_MAX];
    rs_err_t err;
    uint32_t size = 0U;

    if (!rs_args_get_str(parsed, 0U, &path) || path == NULL) {
        ros_puts("usage: run <script>");
        return -1;
    }
    fs_path_abs(rs, path, abs, sizeof(abs));

    /* 校验交给 roshell_fs：存在 / 非目录 / 非空，错误在发起命令的上下文里
     * 立刻反馈，而不是丢到后台任务里。没有字节数上限，执行侧是流式的。 */
    memset(&err, 0, sizeof(err));
    if (rs_file_check(abs, &size, &err) != RS_OK) {
        ros_printf("run: %s: %s\n", abs,
                   (err.msg[0] != '\0') ? err.msg : "cannot run");
        return -1;
    }
    return fs_run_submit(rs, abs, size);
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

/* put 的文本是可变数量实参（逐词拼接），因此不定义参数表：args=NULL 时
 * 分发层跳过解析，直接以 argv 调用处理器，多余实参不会被判为 EXTRA。 */

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
    "put", "create/overwrite file", "put <path> <text>...", NULL,
    NULL, 0U, cmd_put, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_run_def = {
    "run", "run a script file in background <path>", NULL, NULL,
    s_path_arg, 1U, cmd_run, NULL, NULL, NULL, NULL
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
    rc |= rs_cmd_register(&s_run_def);
    rc |= rs_cmd_register(&s_format_def);
    return (rc == 0) ? 0 : -1;
}
