/*
 * roshell_io.c - 基于 ros_io_t 字节流的会话：行编辑 + 行分派
 *
 * 本文件是唯一的“把字节变成行”的实现：roshell_repl（环形缓冲 + 输入任务）
 * 也把字节交给 rs_repl_feed() 处理，两条路径的行编辑行为完全一致。
 */

#include "roshell_io.h"
#include "roshell_cmd.h"
#include "ros.h"

#include <string.h>

/* 输出到会话流；out 为 NULL 时落到当前任务 stdio */
static int rs_io_out(roshell_t *rs, const char *s, uint32_t len)
{
    return ros_io_write((rs != NULL) ? (ros_io_t *)rs->out : NULL, s, len);
}

static void rs_io_prompt(roshell_t *rs)
{
    (void)rs_io_out(rs, "> ", 2U);
}

/* 提交一行：分派 -> 复位行缓冲 -> 补提示符 */
static void rs_io_submit(roshell_t *rs)
{
    rs->line[rs->line_len] = '\0';
    if (rs->line_len > 0U) {
        (void)rs_cmd_dispatch(rs, rs->line);
    }
    rs->line_len = 0U;
    rs->line[0] = '\0';
    if (!rs->exit_req) {
        rs_io_prompt(rs);
    }
}

/* 处理一个输入字节；返回 true 表示提交了一行 */
static bool rs_io_byte(roshell_t *rs, uint8_t c)
{
    if (c == 0x03U) {                       /* Ctrl-C：清行 */
        rs->line_len = 0U;
        rs->last_cr = false;
        rs->line[0] = '\0';
        (void)rs_io_out(rs, "^C\n", 3U);
        rs_io_prompt(rs);
        return false;
    }
    if (c == '\r' || c == '\n') {
        /* 终端可能只发 CR、只发 LF 或发 CRLF：都按一次提交处理 */
        if (c == '\n' && rs->last_cr) {
            rs->last_cr = false;
            return false;
        }
        rs->last_cr = (c == '\r');
        (void)rs_io_out(rs, "\n", 1U);
        rs_io_submit(rs);
        return true;
    }
    rs->last_cr = false;
    if (c == 0x08U || c == 0x7FU) {         /* 退格 */
        if (rs->line_len > 0U) {
            rs->line_len--;
            rs->line[rs->line_len] = '\0';
            (void)rs_io_out(rs, "\b \b", 3U);
        }
        return false;
    }
    if (c == 0x1BU) {                       /* ESC：吞掉方向键等控制序列 */
        rs->last_esc = true;
        return false;
    }
    if (rs->last_esc) {
        if (c == '[' || (c >= '0' && c <= '9') || c == ';') {
            return false;
        }
        rs->last_esc = false;
        return false;
    }
    if (c >= 0x20U && c < 0x7FU) {
        if (rs->line_len + 1U < (uint32_t)RS_LINE_MAX) {   /* 超长行截断 */
            char ch = (char)c;

            rs->line[rs->line_len++] = ch;
            rs->line[rs->line_len] = '\0';
            (void)rs_io_out(rs, &ch, 1U);
        }
    }
    return false;
}

uint32_t rs_repl_feed(roshell_t *rs, const char *data, uint32_t len)
{
    uint32_t lines = 0U;
    uint32_t i;

    if (rs == NULL || !rs->started || data == NULL) {
        return 0U;
    }
    /*
     * 先排空整批输入再响应退出请求：EOF 或 exit 可能在本批数据之前到达
     * （例如输入流已关闭但缓冲里还有几行），此时若立即丢弃剩余字节，
     * 用户最后输入的命令就不会执行。
     */
    for (i = 0U; i < len; i++) {
        if (rs_io_byte(rs, (uint8_t)data[i])) {
            lines++;
        }
    }
    if (rs->exit_req) {
        rs->finished = true;
    }
    return lines;
}

uint32_t rs_repl_poll(roshell_t *rs)
{
    char buf[32];
    uint32_t lines = 0U;
    int n;

    if (rs == NULL || !rs->started) {
        return 0U;
    }
    /* 推进本实例的异步持续命令（'-' 启动），无输入时也要轮转 on_tick */
    rs_cmd_tick_all(rs);
    if (rs->in == NULL) {
        return 0U;
    }
    for (;;) {
        n = ros_io_read((ros_io_t *)rs->in, buf, (uint32_t)sizeof(buf));
        if (n <= 0) {
            if (n < 0) {                    /* 输入关闭：视为 EOF */
                rs->eof = true;
                rs->exit_req = true;
                rs->finished = true;
            }
            break;
        }
        lines += rs_repl_feed(rs, buf, (uint32_t)n);
        if (rs->exit_req) {
            break;                          /* 本批已排空，退出 */
        }
    }
    return lines;
}

int rs_repl_run(roshell_t *rs)
{
    if (rs == NULL || !rs->started) {
        return RS_ERR_PARAM;
    }
    while (!rs->finished) {
        (void)rs_repl_poll(rs);
        if (rs->finished) {
            break;
        }
        /* 有异步持续命令时提高轮转频率，保证 sleep 等命令的精度 */
        ros_sleep_ms(rs_cmd_has_async(rs) ? 1U : 5U);
    }
    return RS_OK;
}

int rs_repl_open(roshell_t *rs, ros_io_t *in, ros_io_t *out, const char *version)
{
    if (rs == NULL) {
        return RS_ERR_PARAM;
    }
    rs->in = in;
    rs->out = out;
    rs->eof = false;
    rs->version = version;
    rs->line_len = 0U;
    rs->line[0] = '\0';
    rs->last_cr = false;
    rs->last_esc = false;
    rs->finished = false;
    rs->exit_req = false;
    rs->cwd[0] = '/';
    rs->cwd[1] = '\0';
    rs_cmd_install(rs);                     /* 脚本块内命令路由（规格 §8） */
    rs->started = true;

    (void)rs_io_out(rs, "roshell ready (", 15U);
    if (version != NULL) {
        (void)rs_io_out(rs, version, (uint32_t)strlen(version));
    } else {
        (void)rs_io_out(rs, "pc", 2U);
    }
    (void)rs_io_out(rs, "): type 'help'\n", 15U);
    rs_io_prompt(rs);
    return RS_OK;
}

void rs_repl_set_out(roshell_t *rs, ros_io_t *out)
{
    if (rs != NULL) {
        rs->out = out;
    }
}

ros_io_t *rs_repl_out(const roshell_t *rs)
{
    return (rs != NULL) ? (ros_io_t *)rs->out : NULL;
}

bool rs_repl_eof(const roshell_t *rs)
{
    return (rs != NULL) && rs->eof;
}

uint32_t rs_repl_line_pending(const roshell_t *rs)
{
    return (rs != NULL) ? rs->line_len : 0U;
}
