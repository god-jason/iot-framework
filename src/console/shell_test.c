/* core/shell.c 单元测试：命令注册/执行、行输入切分与缓冲区边界。 */
#include "test.h"
#include "console.h"

#include <string.h>

static int s_exec_count;
static int s_argc;
static char s_argv0[32];
static char s_argv1[32];
static char s_argv2[32];

static void copy_arg(char *dst, size_t size, const char *src)
{
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, size, "%s", src);
}

static int shell_handler(int argc, char **argv)
{
    s_exec_count++;
    s_argc = argc;
    s_argv0[0] = '\0';
    s_argv1[0] = '\0';
    s_argv2[0] = '\0';
    if (argc > 0) {
        copy_arg(s_argv0, sizeof(s_argv0), argv[0]);
    }
    if (argc > 1) {
        copy_arg(s_argv1, sizeof(s_argv1), argv[1]);
    }
    if (argc > 2) {
        copy_arg(s_argv2, sizeof(s_argv2), argv[2]);
    }
    return 7;
}

void ut_shell(void)
{
    shell_command_t cmd;
    shell_command_t bad;
    int i;

    tf_begin("shell/register 校验与 unregister");
    TF_CHECK_INT(shell_init(), 0);
    TF_CHECK_INT(shell_register(NULL), -1);

    memset(&bad, 0, sizeof(bad));
    bad.description = "bad";
    bad.callback = shell_handler;
    TF_CHECK_INT(shell_register(&bad), -1);      /* name 为 NULL */
    bad.name = "bad";
    bad.callback = NULL;
    TF_CHECK_INT(shell_register(&bad), -1);      /* callback 为 NULL */

    cmd.name = "cmd";
    cmd.description = "test command";
    cmd.callback = shell_handler;
    TF_CHECK_INT(shell_register(&cmd), 0);

    TF_CHECK_INT(shell_unregister(NULL), -1);
    TF_CHECK_INT(shell_unregister("missing"), -1);
    TF_CHECK_INT(shell_unregister("cmd"), 0);
    TF_CHECK_INT(shell_execute("cmd"), -1);
    tf_end();

    tf_begin("shell/execute 参数切分");
    TF_CHECK_INT(shell_init(), 0);
    cmd.name = "cmd";
    cmd.description = NULL;
    cmd.callback = shell_handler;
    TF_CHECK_INT(shell_register(&cmd), 0);

    s_exec_count = 0;
    TF_CHECK_INT(shell_execute("cmd a b"), 7);
    TF_CHECK_INT(s_exec_count, 1);
    TF_CHECK_INT(s_argc, 3);
    TF_CHECK_STR(s_argv0, "cmd");
    TF_CHECK_STR(s_argv1, "a");
    TF_CHECK_STR(s_argv2, "b");

    TF_CHECK_INT(shell_execute("  cmd   a\tb  "), 7);
    TF_CHECK_INT(s_argc, 3);
    TF_CHECK_STR(s_argv1, "a");
    TF_CHECK_STR(s_argv2, "b");

    /* 非法 / 空 / 未知命令 */
    TF_CHECK_INT(shell_execute(NULL), -1);
    TF_CHECK_INT(shell_execute(""), -1);
    TF_CHECK_INT(shell_execute("   "), -1);
    TF_CHECK_INT(shell_execute("unknown x"), -1);
    TF_CHECK_INT(s_exec_count, 2);
    tf_end();

    tf_begin("shell/input 行缓冲");
    TF_CHECK_INT(shell_init(), 0);
    cmd.name = "cmd";
    cmd.description = NULL;
    cmd.callback = shell_handler;
    TF_CHECK_INT(shell_register(&cmd), 0);

    s_exec_count = 0;
    TF_CHECK_INT(shell_input("cmd a\ncmd b\n", 12), 0);
    TF_CHECK_INT(s_exec_count, 2);
    TF_CHECK_INT(s_argc, 2);
    TF_CHECK_STR(s_argv1, "b");

    /* \r 被忽略 */
    TF_CHECK_INT(shell_input("cmd c\r\n", 7), 0);
    TF_CHECK_INT(s_exec_count, 3);
    TF_CHECK_STR(s_argv1, "c");

    /* 分片输入：换行后才执行 */
    s_exec_count = 0;
    TF_CHECK_INT(shell_input("cmd d", 5), 0);
    TF_CHECK_INT(s_exec_count, 0);
    TF_CHECK_INT(shell_input("\n", 1), 0);
    TF_CHECK_INT(s_exec_count, 1);
    TF_CHECK_STR(s_argv1, "d");

    /* 非法参数 */
    TF_CHECK_INT(shell_input(NULL, 3), -1);
    TF_CHECK_INT(shell_input("x", 0), -1);

    /* 行溢出后丢弃并重新开始 */
    TF_CHECK_INT(shell_init(), 0);
    cmd.name = "cmd";
    cmd.description = NULL;
    cmd.callback = shell_handler;
    TF_CHECK_INT(shell_register(&cmd), 0);
    s_exec_count = 0;
    {
        static char long_line[128];
        memset(long_line, 'x', sizeof(long_line));
        TF_CHECK_INT(shell_input(long_line, sizeof(long_line)), 0);
    }
    TF_CHECK_INT(s_exec_count, 0);
    TF_CHECK_INT(shell_input("cmd z\n", 6), 0);
    TF_CHECK_INT(s_exec_count, 1);
    TF_CHECK_STR(s_argv0, "cmd");
    TF_CHECK_STR(s_argv1, "z");
    tf_end();

    tf_begin("shell/命令容量与 process");
    TF_CHECK_INT(shell_init(), 0);
    cmd.name = "dup";
    cmd.description = NULL;
    cmd.callback = shell_handler;
    for (i = 0; i < 16; i++) {
        TF_CHECK_INT(shell_register(&cmd), 0);
    }
    TF_CHECK_INT(shell_register(&cmd), -1);

    TF_CHECK_INT(shell_process(), 0);
    tf_end();
}
