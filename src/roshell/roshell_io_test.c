/*
 * roshell_io_test.c - 流式会话（roshell_io）与按行执行（roshell_fs）用例
 *
 * 两套件都不依赖串口 / 文件系统：
 *   roshell/io-stream   用内存假流验证 ros_io_t 驱动的行编辑与分派；
 *   roshell/lines-exec  用内存行表验证“按行取源”的扫描、执行、错误行号与上限。
 * 真正读文件的 rs_exec_file 由 fs 组件的 run 用例（fs/run-script）覆盖。
 */

#include "test.h"
#include "ros.h"
#include "roshell.h"
#include "roshell_io.h"
#include "roshell_fs.h"
#include "roshell_repl.h"

#include <string.h>

/* ============================== 内存假流 ============================== */

typedef struct {
    const char *data;                       /* 输入片段 */
    uint32_t    len;
    uint32_t    pos;
    bool        eof;                        /* 读尽后返回 -1（EOF） */
    char        out[512];                   /* 输出累计 */
    uint32_t    out_len;
} fake_io_t;

static int fake_read(ros_io_t *io, void *buf, uint32_t cap)
{
    fake_io_t *f = (fake_io_t *)io->user;
    uint32_t n;

    if (f->pos >= f->len) {
        return f->eof ? -1 : 0;             /* 0 = 暂无数据，<0 = 流结束 */
    }
    n = f->len - f->pos;
    if (n > cap) {
        n = cap;
    }
    memcpy(buf, f->data + f->pos, n);
    f->pos += n;
    return (int)n;
}

static int fake_write(ros_io_t *io, const void *buf, uint32_t len)
{
    fake_io_t *f = (fake_io_t *)io->user;

    if (f->out_len + len < sizeof(f->out)) {
        memcpy(f->out + f->out_len, buf, len);
        f->out_len += len;
        f->out[f->out_len] = '\0';
    }
    return (int)len;
}

/* ============================== 内存行表 ============================== */

typedef struct {
    const char *const *lines;               /* 行表（按行索引随机访问） */
    uint16_t           count;
} fake_lines_t;

/* 按行取源：与 roshell.h 的 rs_line_src_t 契约一致 */
static int32_t fake_line(void *ctx, uint16_t idx, char *buf, uint32_t cap)
{
    fake_lines_t *f = (fake_lines_t *)ctx;
    size_t n;

    if (idx >= f->count) {
        return 0;
    }
    n = strlen(f->lines[idx]);
    if (n + 1U > cap) {
        return -2;                          /* 行超长 */
    }
    memcpy(buf, f->lines[idx], n + 1U);
    return (int32_t)n;
}

/* ============================== 用例 ============================== */

static void test_io_stream(void)
{
    fake_io_t in;
    fake_io_t out;
    ros_io_t s_in;
    ros_io_t s_out;
    roshell_t *rs;
    int32_t v = 0;

    tf_begin("roshell/io-stream");

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    memset(&s_in, 0, sizeof(s_in));
    memset(&s_out, 0, sizeof(s_out));
    s_in.read = fake_read;
    s_in.user = &in;
    s_out.write = fake_write;
    s_out.user = &out;

    rs = rs_create();
    TF_CHECK(rs != NULL);
    if (rs == NULL) {
        tf_end();
        return;
    }
    TF_CHECK_INT(rs_repl_open(rs, &s_in, &s_out, "ut"), RS_OK);
    TF_CHECK(rs_shell_started(rs));
    TF_CHECK(strstr(out.out, "roshell ready (ut)") != NULL);
    TF_CHECK(out.out_len > 0U && out.out[out.out_len - 1U] == ' ');

    /* 一行赋值：从流中读入并执行 */
    in.data = "$x = 41\n";
    in.len = 8U;
    in.pos = 0U;
    TF_CHECK_INT(rs_repl_poll(rs), 1U);
    TF_CHECK_INT(rs_get_int(rs, "x", &v), RS_OK);
    TF_CHECK_INT(v, 41);

    /* 一行命令：走命令表，输出到 out 流 */
    in.data = "echo hello\n";
    in.len = 11U;
    in.pos = 0U;
    TF_CHECK_INT(rs_repl_poll(rs), 1U);
    TF_CHECK(strstr(out.out, "hello") != NULL);

    /* 分片投喂：半个行不执行，补齐后才执行 */
    TF_CHECK_INT(rs_repl_line_pending(rs), 0U);
    TF_CHECK_INT(rs_repl_feed(rs, "echo par", 8U), 0U);
    TF_CHECK_INT(rs_repl_line_pending(rs), 8U);
    TF_CHECK_INT(rs_repl_feed(rs, "tial\n", 5U), 1U);
    TF_CHECK_INT(rs_repl_line_pending(rs), 0U);
    TF_CHECK(strstr(out.out, "partial") != NULL);

    /* CRLF 只算一次提交 */
    TF_CHECK_INT(rs_repl_feed(rs, "echo crlf\r\n", 10U), 1U);

    /* EOF 前已到达的行要先执行完，再响应退出（不能丢最后几行） */
    in.eof = true;
    in.pos = in.len = 0U;
    TF_CHECK_INT(rs_repl_feed(rs, "echo last\n", 10U), 1U);
    TF_CHECK(strstr(out.out, "last") != NULL);
    (void)rs_repl_poll(rs);
    TF_CHECK(rs_repl_eof(rs));
    TF_CHECK(rs_shell_task_finished(rs));

    rs_destroy(rs);
    tf_end();
}

/* 脚本行表：函数 + for 回跳 + if/elseif 分支 */
static const char *const s_script_lines[] = {
    "function twice($a)",
    "return $a * 2",
    "end",
    "$i = 0",
    "for $i = 1,2,3 do",
    "$x = twice($i)",
    "end",
    "$v = 5",
    "if $v > 10",
    "$r = 1",
    "elseif $v > 3",
    "$r = 2",
    "else",
    "$r = 3",
    "end"
};

static const char *const s_bad_lines[] = {
    "$a = 1",
    "$b ="
};

static void test_lines_exec(void)
{
    fake_lines_t fl;
    roshell_t *rs;
    rs_err_t err;
    int32_t v = 0;

    tf_begin("roshell/lines-exec");

    rs = rs_create();
    TF_CHECK(rs != NULL);
    if (rs == NULL) {
        tf_end();
        return;
    }

    memset(&fl, 0, sizeof(fl));
    fl.lines = s_script_lines;
    fl.count = (uint16_t)(sizeof(s_script_lines) / sizeof(s_script_lines[0]));
    memset(&err, 0, sizeof(err));
    TF_CHECK_INT(rs_exec_lines(rs, fake_line, &fl, fl.count, &err), RS_OK);

    /* 函数调用 + for 回跳 + if/elseif 分支：全部靠行分类表跳转，不读原文 */
    TF_CHECK_INT(rs_get_int(rs, "x", &v), RS_OK);
    TF_CHECK_INT(v, 6);
    TF_CHECK_INT(rs_get_int(rs, "i", &v), RS_OK);
    TF_CHECK_INT(v, 3);
    TF_CHECK_INT(rs_get_int(rs, "r", &v), RS_OK);
    TF_CHECK_INT(v, 2);

    /* 语法错误要带正确行号（第 2 行赋值缺右值） */
    memset(&fl, 0, sizeof(fl));
    fl.lines = s_bad_lines;
    fl.count = 2U;
    memset(&err, 0, sizeof(err));
    TF_CHECK(rs_exec_lines(rs, fake_line, &fl, fl.count, &err) != RS_OK);
    TF_CHECK_INT(err.line, 2);
    TF_CHECK(err.msg[0] != '\0');

    /* 行数上限：超过 RS_LINE_SRC_MAX 直接报 too many lines */
    memset(&err, 0, sizeof(err));
    fl.count = (uint16_t)(RS_LINE_SRC_MAX + 4U);
    TF_CHECK_INT(rs_exec_lines(rs, fake_line, &fl, fl.count, &err),
                 RS_ERR_LIMIT);
    TF_CHECK_INT(err.code, RS_ERR_LIMIT);

    /* 参数校验 */
    TF_CHECK_INT(rs_exec_lines(rs, NULL, &fl, 1U, &err), RS_ERR_PARAM);
    TF_CHECK_INT(rs_exec_lines(rs, fake_line, &fl, 0U, &err), RS_ERR_PARAM);

    rs_destroy(rs);
    tf_end();
}

void ut_roshell_io(void)
{
    test_io_stream();
    test_lines_exec();
}
