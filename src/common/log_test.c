/*
 * log_test.c - 日志宏测试（对应 log.c）
 *
 * 只验证宏能正常展开、等级可读写、hexdump 对 NULL/零长度安全，
 * 不断言 stdout 内容。
 */

#include "test.h"
#include "ros.h"
#include "common.h"

void ut_log(void)
{
    static const uint8_t data[17] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x10
    };

    tf_begin("log/等级读写");

    TF_CHECK_INT(log_get_level(), LOG_LEVEL);

    log_set_level(LOG_LEVEL_DEBUG);
    TF_CHECK_INT(log_get_level(), LOG_LEVEL_DEBUG);

    log_set_level(LOG_LEVEL_ERROR);
    TF_CHECK_INT(log_get_level(), LOG_LEVEL_ERROR);

    log_set_level(LOG_LEVEL);
    TF_CHECK_INT(log_get_level(), LOG_LEVEL);

    tf_end();

    tf_begin("log/宏展开");

    LOG_D("TST", "debug %d", 1);
    LOG_I("TST", "info %s", "msg");
    LOG_W("TST", "warn");
    LOG_E("TST", "error");
    LOG_F("TST", "fatal");
    LOG_EF("TST", "fatal alias");
    TF_CHECK(1);

    tf_end();

    tf_begin("log/hexdump 边界");

    LOG_HEXDUMP(LOG_LEVEL_INFO, "HD", data, sizeof(data));
    log_hexdump(LOG_LEVEL_DEBUG, NULL, data, 3U);
    log_hexdump(LOG_LEVEL_INFO, "HD", NULL, 8U);
    log_hexdump(LOG_LEVEL_INFO, "HD", data, 0U);
    log_hexdump(LOG_LEVEL_NONE, "HD", data, sizeof(data));
    TF_CHECK(1);   /* NULL / 零长度不得崩溃 */

    tf_end();
}
