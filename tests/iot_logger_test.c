/* core/iot_logger.c 单元测试：只验证返回值、过滤逻辑与回调内容，不断言 stdout。 */
#include "test_framework.h"
#include "iot_logger.h"

#include <string.h>

static int s_cnt_a;
static int s_calls;
static int s_msg_count;
static iot_log_level_t s_level_seen;
static char s_tag_seen[64];
static char s_msgs[8][160];

static void out_a(iot_log_level_t level, const char *tag,
                  const char *message, void *user)
{
    (void)level;
    (void)tag;
    (void)message;
    (void)user;
    s_cnt_a++;
}

static void capture_output(iot_log_level_t level, const char *tag,
                           const char *message, void *user)
{
    (void)user;
    s_calls++;
    s_level_seen = level;
    if (tag != NULL) {
        snprintf(s_tag_seen, sizeof(s_tag_seen), "%s", tag);
    } else {
        s_tag_seen[0] = '\0';
    }
    if (s_msg_count < 8) {
        snprintf(s_msgs[s_msg_count], sizeof(s_msgs[0]), "%s", message);
        s_msg_count++;
    }
}

void ut_iot_logger(void)
{
    iot_log_backend_t be;
    iot_log_backend_t be_warn;
    iot_log_backend_t bad;
    int i;

    tf_begin("logger/init 与 level");
    TF_CHECK_INT(iot_log_init(), IOT_OK);
    TF_CHECK_INT(iot_log_get_level(), IOT_LOG_INFO);
    TF_CHECK_INT(iot_log_set_level(IOT_LOG_DEBUG), IOT_OK);
    TF_CHECK_INT(iot_log_get_level(), IOT_LOG_DEBUG);
    TF_CHECK_INT(iot_log_set_level(IOT_LOG_FATAL), IOT_OK);
    TF_CHECK_INT(iot_log_get_level(), IOT_LOG_FATAL);
    /* 越界等级 */
    TF_CHECK_INT(iot_log_set_level((iot_log_level_t)5), -1);
    TF_CHECK_INT(iot_log_set_level((iot_log_level_t)-1), -1);
    TF_CHECK_INT(iot_log_get_level(), IOT_LOG_FATAL);
    tf_end();

    tf_begin("logger/register 与 unregister");
    TF_CHECK_INT(iot_log_init(), IOT_OK);
    TF_CHECK_INT(iot_log_register(NULL), -1);
    bad.level = IOT_LOG_DEBUG;
    bad.tag = "X";
    bad.output = NULL;
    bad.user = NULL;
    TF_CHECK_INT(iot_log_register(&bad), -1);

    be.level = IOT_LOG_DEBUG;
    be.tag = "T";
    be.output = out_a;
    be.user = NULL;
    TF_CHECK_INT(iot_log_register(&be), IOT_OK);
    TF_CHECK_INT(iot_log_unregister(NULL), -1);
    TF_CHECK_INT(iot_log_unregister(out_a), IOT_OK);
    /* 已移除，再次移除失败 */
    TF_CHECK_INT(iot_log_unregister(out_a), -1);
    tf_end();

    tf_begin("logger/backend 容量");
    TF_CHECK_INT(iot_log_init(), IOT_OK);
    s_cnt_a = 0;
    be.level = IOT_LOG_DEBUG;
    be.tag = "T";
    be.output = out_a;
    be.user = NULL;
    for (i = 0; i < 4; i++) {
        TF_CHECK_INT(iot_log_register(&be), IOT_OK);
    }
    /* 最多 4 个 backend */
    TF_CHECK_INT(iot_log_register(&be), -1);
    TF_CHECK_INT(iot_log_unregister(out_a), IOT_OK);
    TF_CHECK_INT(iot_log_register(&be), IOT_OK);
    tf_end();

    tf_begin("logger/等级过滤");
    TF_CHECK_INT(iot_log_init(), IOT_OK);
    TF_CHECK_INT(iot_log_set_level(IOT_LOG_INFO), IOT_OK);
    s_calls = 0;
    s_msg_count = 0;
    be.level = IOT_LOG_DEBUG;
    be.tag = "MOD";
    be.output = capture_output;
    be.user = NULL;
    TF_CHECK_INT(iot_log_register(&be), IOT_OK);

    iot_log_write(IOT_LOG_DEBUG, "MOD", "hidden");
    TF_CHECK_INT(s_calls, 0);
    iot_log_write(IOT_LOG_INFO, "MOD", "v=%d", 42);
    TF_CHECK_INT(s_calls, 1);
    TF_CHECK_INT(s_level_seen, IOT_LOG_INFO);
    TF_CHECK_STR(s_tag_seen, "MOD");
    TF_CHECK_STR(s_msgs[0], "v=42");

    /* backend 自身最低等级过滤 */
    TF_CHECK_INT(iot_log_init(), IOT_OK);
    TF_CHECK_INT(iot_log_set_level(IOT_LOG_DEBUG), IOT_OK);
    s_calls = 0;
    s_msg_count = 0;
    be_warn.level = IOT_LOG_WARN;
    be_warn.tag = "MOD";
    be_warn.output = capture_output;
    be_warn.user = NULL;
    TF_CHECK_INT(iot_log_register(&be_warn), IOT_OK);
    iot_log_write(IOT_LOG_INFO, "MOD", "info");
    TF_CHECK_INT(s_calls, 0);
    iot_log_write(IOT_LOG_WARN, "MOD", "warn");
    TF_CHECK_INT(s_calls, 1);
    iot_log_write(IOT_LOG_ERROR, "MOD", "error");
    TF_CHECK_INT(s_calls, 2);
    tf_end();

    tf_begin("logger/hexdump");
    TF_CHECK_INT(iot_log_init(), IOT_OK);
    TF_CHECK_INT(iot_log_set_level(IOT_LOG_DEBUG), IOT_OK);
    s_calls = 0;
    s_msg_count = 0;
    be.level = IOT_LOG_DEBUG;
    be.tag = "HD";
    be.output = capture_output;
    be.user = NULL;
    TF_CHECK_INT(iot_log_register(&be), IOT_OK);

    {
        static const uint8_t data3[] = {0x01, 0x02, 0x03};
        iot_log_hexdump(IOT_LOG_INFO, "HD", data3, 3);
    }
    TF_CHECK_INT(s_msg_count, 1);
    TF_CHECK(strstr(s_msgs[0], "0000: 01 02 03") != NULL);

    s_calls = 0;
    s_msg_count = 0;
    {
        uint8_t data17[17];
        memset(data17, 0xAB, sizeof(data17));
        iot_log_hexdump(IOT_LOG_INFO, "HD", data17, 17);
    }
    TF_CHECK_INT(s_msg_count, 2);
    TF_CHECK(strstr(s_msgs[0], "0000:") != NULL);
    TF_CHECK(strstr(s_msgs[1], "0010:") != NULL);

    /* len == 0 / data == NULL / 等级不足：均不输出 */
    s_calls = 0;
    iot_log_hexdump(IOT_LOG_INFO, "HD", NULL, 8);
    TF_CHECK_INT(s_calls, 0);
    {
        static const uint8_t data1[] = {0x01};
        iot_log_hexdump(IOT_LOG_INFO, "HD", data1, 0);
        TF_CHECK_INT(s_calls, 0);
        iot_log_set_level(IOT_LOG_ERROR);
        iot_log_hexdump(IOT_LOG_INFO, "HD", data1, 1);
        TF_CHECK_INT(s_calls, 0);
    }
    tf_end();
}
