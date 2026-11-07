/* core/iot_settings.c 单元测试：注册默认值、RAM 读写、重置与容量边界。 */
#include "test_framework.h"
#include "iot_settings.h"

#include <string.h>

static uint8_t s_byte_def = 0x5A;
static uint32_t s_u32_def = 7;
static uint32_t s_u32_def2 = 100;
static uint8_t s_big_def[300];

void ut_iot_settings(void)
{
    iot_settings_item_t item;
    iot_settings_item_t item2;
    uint32_t v;
    uint8_t small;

    tf_begin("settings/register 参数校验");
    TF_CHECK_INT(iot_settings_init(), IOT_OK);
    TF_CHECK_INT(iot_settings_register(NULL), -1);

    item.name = NULL;
    item.default_value = &s_u32_def;
    item.size = sizeof(s_u32_def);
    TF_CHECK_INT(iot_settings_register(&item), -1);

    item.name = "vol";
    item.default_value = NULL;
    TF_CHECK_INT(iot_settings_register(&item), -1);

    item.default_value = &s_u32_def;
    item.size = 0;
    TF_CHECK_INT(iot_settings_register(&item), -1);
    tf_end();

    tf_begin("settings/get 与 set");
    TF_CHECK_INT(iot_settings_init(), IOT_OK);
    item.name = "vol";
    item.default_value = &s_u32_def;
    item.size = sizeof(s_u32_def);
    TF_CHECK_INT(iot_settings_register(&item), IOT_OK);

    v = 0;
    TF_CHECK_INT(iot_settings_get("vol", &v, sizeof(v)), IOT_OK);
    TF_CHECK_INT(v, 7);

    v = 42;
    TF_CHECK_INT(iot_settings_set("vol", &v, sizeof(v)), IOT_OK);
    v = 0;
    TF_CHECK_INT(iot_settings_get("vol", &v, sizeof(v)), IOT_OK);
    TF_CHECK_INT(v, 42);

    /* 传入长度大于项大小时按项大小截断 */
    {
        uint32_t big = 0x11223344u;
        v = 0;
        TF_CHECK_INT(iot_settings_set("vol", &big, sizeof(big) + 4), IOT_OK);
        TF_CHECK_INT(iot_settings_get("vol", &v, sizeof(v)), IOT_OK);
        TF_CHECK_INT(v, 0x11223344u);
    }

    /* 传入长度小于项大小时只拷贝前若干字节 */
    small = 0x7F;
    TF_CHECK_INT(iot_settings_get("vol", &small, sizeof(small)), IOT_OK);

    /* size 为 0 时返回 OK 且不写入 */
    small = 0x33;
    TF_CHECK_INT(iot_settings_get("vol", &small, 0), IOT_OK);
    TF_CHECK_INT(small, 0x33);

    /* 非法参数 */
    TF_CHECK_INT(iot_settings_get("missing", &v, sizeof(v)), -1);
    TF_CHECK_INT(iot_settings_get(NULL, &v, sizeof(v)), -1);
    TF_CHECK_INT(iot_settings_get("vol", NULL, sizeof(v)), -1);
    TF_CHECK_INT(iot_settings_set("missing", &v, sizeof(v)), -1);
    TF_CHECK_INT(iot_settings_set(NULL, &v, sizeof(v)), -1);
    TF_CHECK_INT(iot_settings_set("vol", NULL, sizeof(v)), -1);
    tf_end();

    tf_begin("settings/reset 与 reset_all");
    TF_CHECK_INT(iot_settings_init(), IOT_OK);
    item.name = "vol";
    item.default_value = &s_u32_def;
    item.size = sizeof(s_u32_def);
    item2.name = "limit";
    item2.default_value = &s_u32_def2;
    item2.size = sizeof(s_u32_def2);
    TF_CHECK_INT(iot_settings_register(&item), IOT_OK);
    TF_CHECK_INT(iot_settings_register(&item2), IOT_OK);

    v = 1;
    TF_CHECK_INT(iot_settings_set("vol", &v, sizeof(v)), IOT_OK);
    TF_CHECK_INT(iot_settings_set("limit", &v, sizeof(v)), IOT_OK);
    TF_CHECK_INT(iot_settings_reset("vol"), IOT_OK);
    TF_CHECK_INT(iot_settings_get("vol", &v, sizeof(v)), IOT_OK);
    TF_CHECK_INT(v, 7);
    TF_CHECK_INT(iot_settings_get("limit", &v, sizeof(v)), IOT_OK);
    TF_CHECK_INT(v, 1);

    TF_CHECK_INT(iot_settings_reset("missing"), -1);
    TF_CHECK_INT(iot_settings_reset(NULL), -1);

    TF_CHECK_INT(iot_settings_reset_all(), IOT_OK);
    TF_CHECK_INT(iot_settings_get("limit", &v, sizeof(v)), IOT_OK);
    TF_CHECK_INT(v, 100);
    tf_end();

    tf_begin("settings/条目容量上限");
    TF_CHECK_INT(iot_settings_init(), IOT_OK);
    item.name = "dup";
    item.default_value = &s_byte_def;
    item.size = sizeof(s_byte_def);
    {
        int i;
        for (i = 0; i < 32; i++) {
            TF_CHECK_INT(iot_settings_register(&item), IOT_OK);
        }
    }
    TF_CHECK_INT(iot_settings_register(&item), -1);
    TF_CHECK_INT(iot_settings_init(), IOT_OK);
    tf_end();

    tf_begin("settings/缓冲区容量上限");
    TF_CHECK_INT(iot_settings_init(), IOT_OK);
    memset(s_big_def, 0x11, sizeof(s_big_def));
    item.name = "big";
    item.default_value = s_big_def;
    item.size = sizeof(s_big_def);
    TF_CHECK_INT(iot_settings_register(&item), IOT_OK);
    /* 300 + 300 超过 512 字节缓冲区 */
    TF_CHECK_INT(iot_settings_register(&item), -1);
    tf_end();
}
