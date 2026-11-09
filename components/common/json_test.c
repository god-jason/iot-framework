/*
 * json_test.c - JSON 宏封装测试（对应 json.h / vendor cJSON）
 *
 * 覆盖：文本解析、字段读取、结构体到 JSON 生成、数组处理、
 *       类型判断与非法输入。
 */

#include "test.h"
#include "json.h"

typedef struct {
    int id;
    char name[32];
    bool enabled;
    float temperature;
    float values[4];
} device_t;

void ut_json(void)
{
    static const char *text =
        "{\"id\":1001,\"name\":\"sensor01\",\"enabled\":true,"
        "\"temperature\":25.5,\"values\":[1.1,2.2,3.3,4.4]}";
    device_t dev;

    tf_begin("json/解析文本到结构体");

    memset(&dev, 0, sizeof(dev));
    {
        cJSON *root = JSON_PARSE_TEXT(text);

        TF_CHECK(root != NULL);
        TF_CHECK(JSON_IS_OBJECT(root));

        if (root != NULL) {
            JSON_PARSE_INT_NAME(root, dev.id, "id");
            JSON_PARSE_STRING_NAME(root, dev.name, "name", sizeof(dev.name));
            JSON_PARSE_BOOL_NAME(root, dev.enabled, "enabled");
            JSON_PARSE_FLOAT_NAME(root, dev.temperature, "temperature");
            JSON_PARSE_ARRAY_FLOAT_NAME(root, dev.values, "values", 4);
            JSON_DELETE(root);
        }
    }

    TF_CHECK_INT(dev.id, 1001);
    TF_CHECK_STR(dev.name, "sensor01");
    TF_CHECK(dev.enabled);
    TF_CHECK_NEAR(dev.temperature, 25.5f, 1e-4);
    TF_CHECK_NEAR(dev.values[0], 1.1f, 1e-4);
    TF_CHECK_NEAR(dev.values[3], 4.4f, 1e-4);

    tf_end();

    tf_begin("json/缺失字段保持原值");

    {
        device_t d2;
        cJSON *root;

        memset(&d2, 0, sizeof(d2));
        d2.id = 7;
        d2.temperature = 3.0f;

        root = JSON_PARSE_TEXT("{\"other\":1}");
        TF_CHECK(root != NULL);
        if (root != NULL) {
            JSON_PARSE_INT_NAME(root, d2.id, "id");
            JSON_PARSE_FLOAT_NAME(root, d2.temperature, "temperature");
            JSON_DELETE(root);
        }
        TF_CHECK_INT(d2.id, 7);
        TF_CHECK_NEAR(d2.temperature, 3.0f, 1e-6);
    }

    tf_end();

    tf_begin("json/结构体生成文本");

    memset(&dev, 0, sizeof(dev));
    dev.id = 42;
    strncpy(dev.name, "arm01", sizeof(dev.name) - 1U);
    dev.enabled = false;
    dev.temperature = -1.5f;
    dev.values[0] = 0.5f;
    dev.values[1] = 1.5f;
    dev.values[2] = 2.5f;
    dev.values[3] = 3.5f;

    {
        cJSON *root = JSON_CREATE();

        TF_CHECK(root != NULL);
        if (root != NULL) {
            char *output;

            TF_CHECK(JSON_ADD_INT_NAME(root, dev.id, "id") != NULL);
            TF_CHECK(JSON_ADD_STRING_NAME(root, dev.name, "name") != NULL);
            TF_CHECK(JSON_ADD_BOOL_NAME(root, dev.enabled, "enabled") != NULL);
            TF_CHECK(JSON_ADD_FLOAT_NAME(root, dev.temperature,
                                         "temperature") != NULL);
            JSON_ADD_ARRAY_FLOAT_NAME(root, dev.values, "values", 4);

            output = JSON_PRINT(root);
            TF_CHECK(output != NULL);
            if (output != NULL) {
                TF_CHECK(strstr(output, "\"id\"") != NULL);
                TF_CHECK(strstr(output, "arm01") != NULL);
                TF_CHECK(strstr(output, "\"values\"") != NULL);
                JSON_FREE_STRING(output);
            }
            JSON_DELETE(root);
        }
    }

    tf_end();

    tf_begin("json/类型判断与非法输入");

    TF_CHECK(JSON_PARSE_TEXT("not json") == NULL);
    TF_CHECK(JSON_PARSE_TEXT(NULL) == NULL);

    {
        cJSON *root = JSON_PARSE_TEXT("{\"n\":null,\"b\":false,\"s\":\"x\"}");

        TF_CHECK(root != NULL);
        if (root != NULL) {
            cJSON *n = cJSON_GetObjectItem(root, "n");
            cJSON *b = cJSON_GetObjectItem(root, "b");
            cJSON *s = cJSON_GetObjectItem(root, "s");

            TF_CHECK(JSON_IS_NULL(n));
            TF_CHECK(JSON_IS_BOOL(b));
            TF_CHECK(JSON_IS_STRING(s));
            TF_CHECK(!JSON_IS_ARRAY(s));
            TF_CHECK(JSON_IS_NULL(NULL));
            JSON_DELETE(root);
        }
    }

    tf_end();
}
