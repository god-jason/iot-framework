/* log.c 单元测试：宏日志调用与十六进制转储边界（不断言 stdout 内容）。 */
#include "test.h"
#include "log.h"

void ut_log(void)
{
    static const uint8_t data[17] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x10
    };

    tf_begin("log/等级宏调用");
    TF_CHECK_INT(log_get_level(), LOG_LEVEL);
    log_set_level(LOG_LEVEL_DEBUG);
    TF_CHECK_INT(log_get_level(), LOG_LEVEL_DEBUG);
    LOG_D("TST", "debug %d", 1);
    LOG_I("TST", "info");
    LOG_W("TST", "warn");
    LOG_E("TST", "error");
    LOG_F("TST", "fatal");
    LOG_EF("TST", "fatal alias");
    log_set_level(LOG_LEVEL);
    TF_CHECK(1);   /* 编译期过滤后仍可正常展开与调用 */
    tf_end();

    tf_begin("log/hexdump 与边界");
    LOG_HEXDUMP(LOG_LEVEL_INFO, "HD", data, sizeof(data));
    log_hexdump(LOG_LEVEL_DEBUG, NULL, data, 3);
    log_hexdump(LOG_LEVEL_INFO, "HD", NULL, 8);
    log_hexdump(LOG_LEVEL_INFO, "HD", data, 0);
    TF_CHECK(1);   /* NULL/零长度不得崩溃 */
    tf_end();
}
