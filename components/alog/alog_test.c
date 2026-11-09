/*
 * alog_test.c - 异步日志缓冲测试（对应 alog.c）
 *
 * 覆盖：基本读写、溢出覆盖最旧数据、丢弃计数、清空与 NULL 安全。
 */

#include "test.h"
#include "alog.h"

void ut_alog(void)
{
    char buf[4];
    char out[8];
    alog_t log;
    uint32_t n;

    tf_begin("alog/初始化");

    TF_CHECK_INT(alog_init(NULL, buf, 4U), -1);
    TF_CHECK_INT(alog_init(&log, NULL, 4U), -1);
    TF_CHECK_INT(alog_init(&log, buf, 0U), -1);
    TF_CHECK_INT(alog_init(&log, buf, sizeof(buf)), 0);
    TF_CHECK_INT(alog_count(&log), 0);
    TF_CHECK_INT(alog_dropped(&log), 0);

    tf_end();

    tf_begin("alog/写入与读取顺序");

    TF_CHECK_INT(alog_write(&log, "abc", 3U), 3);
    TF_CHECK_INT(alog_count(&log), 3);

    n = alog_read(&log, out, 2U);
    TF_CHECK_INT(n, 2);
    TF_CHECK_INT(out[0], 'a');
    TF_CHECK_INT(out[1], 'b');
    TF_CHECK_INT(alog_count(&log), 1);

    n = alog_read(&log, out, sizeof(out));
    TF_CHECK_INT(n, 1);
    TF_CHECK_INT(out[0], 'c');
    TF_CHECK_INT(alog_count(&log), 0);

    tf_end();

    tf_begin("alog/溢出覆盖最旧数据");

    alog_clear(&log);
    TF_CHECK_INT(alog_write(&log, "abcdef", 6U), 6);
    TF_CHECK_INT(alog_count(&log), 4);        /* 容量 4，保留最后 4 字节 */
    TF_CHECK_INT(alog_dropped(&log), 2);

    n = alog_read(&log, out, sizeof(out));
    TF_CHECK_INT(n, 4);
    TF_CHECK_INT(out[0], 'c');
    TF_CHECK_INT(out[3], 'f');

    tf_end();

    tf_begin("alog/边界与 NULL 安全");

    TF_CHECK_INT(alog_read(&log, out, sizeof(out)), 0);
    TF_CHECK_INT(alog_write(&log, NULL, 1U), 0);
    TF_CHECK_INT(alog_write(&log, "x", 0U), 0);
    TF_CHECK_INT(alog_read(&log, NULL, 1U), 0);
    TF_CHECK_INT(alog_read(NULL, out, 1U), 0);
    TF_CHECK_INT(alog_count(NULL), 0);
    TF_CHECK_INT(alog_dropped(NULL), 0);

    /* 单字节写入后立即读回 */
    TF_CHECK_INT(alog_write(&log, "Z", 1U), 1);
    TF_CHECK_INT(alog_read(&log, out, 1U), 1);
    TF_CHECK_INT(out[0], 'Z');

    tf_end();
}
