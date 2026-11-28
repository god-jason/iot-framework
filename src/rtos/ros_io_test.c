/*
 * ros_io_test.c - 标准输入输出抽象回归测试
 */

#include "ros.h"
#include "common.h"
#include "test.h"

#include <string.h>

static char s_out[64];
static uint32_t s_out_len;
static const char *s_in = "abc";
static uint32_t s_in_pos;

static int fake_read(ros_io_t *io, void *buf, uint32_t len)
{
    uint32_t left = (uint32_t)strlen(s_in) - s_in_pos;

    (void)io;
    if (left == 0U) {
        return 0;
    }
    if (len > left) {
        len = left;
    }
    memcpy(buf, &s_in[s_in_pos], len);
    s_in_pos += len;
    return (int)len;
}

static int fake_write(ros_io_t *io, const void *buf, uint32_t len)
{
    uint32_t n = len;

    (void)io;
    if (n > sizeof(s_out) - s_out_len) {
        n = (uint32_t)sizeof(s_out) - s_out_len;
    }
    memcpy(&s_out[s_out_len], buf, n);
    s_out_len += n;
    return (int)len;
}

static int fake_close(ros_io_t *io)
{
    (void)io;
    return 0;
}

static ros_io_t s_fake = { fake_read, fake_write, fake_close, NULL };

static void io_task_entry(void *arg)
{
    (void)arg;
}

void ut_io(void)
{
    ros_io_t *saved_default = ros_io_default();
    ros_io_t *saved_stdio = ros_stdio();
    ros_task_t *task;

    tf_begin("ros/标准输入输出");

    /* 默认 stdio 注册与查询 */
    ros_io_set_default(&s_fake);
    TF_CHECK(ros_io_default() == &s_fake);

    /* 显式 io 读写 */
    s_out_len = 0U;
    TF_CHECK_INT(ros_io_write(&s_fake, "hi", 2U), 2);
    TF_CHECK_INT(ros_io_puts(&s_fake, "ok"), 2);
    TF_CHECK_INT((int)s_out_len, 4);
    TF_CHECK(s_out_len == 4U && memcmp(s_out, "hiok", 4) == 0);

    s_in_pos = 0U;
    {
        char b[8];
        int n = ros_io_read(&s_fake, b, 2U);

        TF_CHECK_INT(n, 2);
        TF_CHECK(n == 2 && memcmp(b, "ab", 2) == 0);
        n = ros_io_read(&s_fake, b, (uint32_t)sizeof(b));
        TF_CHECK_INT(n, 1);
        TF_CHECK(n == 1 && b[0] == 'c');
        TF_CHECK_INT(ros_io_read(&s_fake, b, 1U), 0);   /* 已读完 */
    }

    /* null io：丢弃输出、无输入 */
    TF_CHECK_INT(ros_io_write(ros_io_null(), "x", 1U), 1);
    {
        char b[1];

        TF_CHECK_INT(ros_io_read(ros_io_null(), b, 1U), 0);
    }

    /* 当前任务 stdio 绑定（ut_ros 之后调度器已停止，任务上下文可能仍在） */
    if (ros_current_task != NULL) {
        TF_CHECK_INT(ros_stdio_set(&s_fake), 0);
        TF_CHECK(ros_stdio() == &s_fake);
    } else {
        TF_CHECK_INT(ros_stdio_set(&s_fake), -1);
    }

    /* stdio 风格输出：统一落到 ros_stdio()（此处为 s_fake） */
    s_out_len = 0U;
    TF_CHECK_INT(ros_putchar('a'), 1);
    TF_CHECK_INT(ros_puts("bc"), 3);                /* "bc\n" */
    TF_CHECK_INT(ros_printf("%d:%s", 7, "z"), 3);
    TF_CHECK_INT((int)s_out_len, 7);
    TF_CHECK(s_out_len == 7U && memcmp(s_out, "abc\n7:z", 7) == 0);

    /* stdio 风格输入 */
    s_in = "42 hi\n";
    s_in_pos = 0U;
    TF_CHECK_INT(ros_getchar(), '4');
    s_in_pos = 0U;
    {
        int iv = 0;
        char word[8] = { 0 };

        TF_CHECK_INT(ros_scanf("%d %s", &iv, word), 2);
        TF_CHECK_INT(iv, 42);
        TF_CHECK(memcmp(word, "hi", 3) == 0);
    }

    s_in = "abc\n";
    s_in_pos = 0U;
    {
        char line[8];

        TF_CHECK_INT(ros_gets(line, (uint32_t)sizeof(line)), 3);
        TF_CHECK(memcmp(line, "abc", 4) == 0);
    }

    if (ros_current_task != NULL) {
        TF_CHECK_INT(ros_stdio_set(saved_stdio), 0);
    }

    /* 创建任务时显式指定 stdio */
    task = ros_task_create_dynamic_io("iow", 5U, io_task_entry, NULL, 1024U,
                                      ros_io_null());
    TF_CHECK(task != NULL);
    if (task != NULL) {
        TF_CHECK(ros_task_get_stdio(task) == ros_io_null());
        TF_CHECK_INT(ros_task_set_stdio(task, &s_fake), ROS_OK);
        TF_CHECK(ros_task_get_stdio(task) == &s_fake);
        TF_CHECK_INT(ros_task_destroy(task), ROS_OK);
    }

    /* 未指定时继承创建者的 stdio */
    {
        ros_io_t *inherit = ros_stdio();

        task = ros_task_create_dynamic("iow2", 5U, io_task_entry, NULL, 1024U);
        TF_CHECK(task != NULL);
        if (task != NULL) {
            TF_CHECK(ros_task_get_stdio(task) == inherit);
            TF_CHECK_INT(ros_task_destroy(task), ROS_OK);
        }
    }

    ros_io_set_default(saved_default);
    tf_end();
}
