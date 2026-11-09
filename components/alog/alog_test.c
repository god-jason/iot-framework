/* alog 单元测试：环形读写、覆盖与丢弃计数。 */
#include "test.h"
#include "alog.h"

void ut_alog(void)
{
    char buf[4];
    char out[8];
    alog_t log;
    uint32_t n;

    tf_begin("alog/初始化与基本读写");
    TF_CHECK_INT(alog_init(NULL, buf, 4), -1);
    TF_CHECK_INT(alog_init(&log, NULL, 4), -1);
    TF_CHECK_INT(alog_init(&log, buf, 0), -1);
    TF_CHECK_INT(alog_init(&log, buf, 4), 0);

    TF_CHECK_INT(alog_write(&log, "abc", 3), 3);
    TF_CHECK_INT(alog_count(&log), 3);

    n = alog_read(&log, out, 2);
    TF_CHECK_INT(n, 2);
    TF_CHECK_INT(out[0], 'a');
    TF_CHECK_INT(out[1], 'b');
    TF_CHECK_INT(alog_count(&log), 1);

    n = alog_read(&log, out, 8);
    TF_CHECK_INT(n, 1);
    TF_CHECK_INT(out[0], 'c');
    TF_CHECK_INT(alog_count(&log), 0);
    tf_end();

    tf_begin("alog/溢出覆盖最旧数据");
    alog_clear(&log);
    TF_CHECK_INT(alog_write(&log, "abcdef", 6), 6);
    TF_CHECK_INT(alog_count(&log), 4);
    TF_CHECK_INT(alog_dropped(&log), 2);

    n = alog_read(&log, out, 4);
    TF_CHECK_INT(n, 4);
    TF_CHECK_INT(out[0], 'c');
    TF_CHECK_INT(out[1], 'd');
    TF_CHECK_INT(out[2], 'e');
    TF_CHECK_INT(out[3], 'f');
    tf_end();

    tf_begin("alog/空缓冲与非法参数");
    TF_CHECK_INT(alog_read(&log, out, 4), 0);
    TF_CHECK_INT(alog_write(&log, NULL, 1), 0);
    TF_CHECK_INT(alog_read(NULL, out, 4), 0);
    tf_end();
}
