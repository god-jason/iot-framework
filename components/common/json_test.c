/* json.h 宏封装测试：解析、生成、默认值、别名、数组与节点操作。 */
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
        "{\"id\":1001,\"name\":\"sensor01\","
        "\"enabled\":true,\"temperature\":25.5,"
        "\"values\":[1.1,2.2,3.3,4.4]}";
    device_t dev;
    cJSON *root;
    char *output;
    int64_t i64 = 0;
    uint64_t u64 = 0;

    tf_begin("json/JSON 解析到结构体");
    memset(&dev, 0, sizeof(dev));
    root = JSON_PARSE_TEXT(text);
    TF_CHECK(root != NULL);
    if (root != NULL) {
        JSON_PARSE_INT_NAME(root, dev.id, "id");
        JSON_PARSE_STRING_NAME(root, dev.name, "name", sizeof(dev.name));
        JSON_PARSE_BOOL_NAME(root, dev.enabled, "enabled");
        JSON_PARSE_FLOAT_NAME(root, dev.temperature, "temperature");
        JSON_PARSE_ARRAY_FLOAT_NAME(root, dev.values, "values", 4);

        TF_CHECK_INT(dev.id, 1001);
        TF_CHECK_STR(dev.name, "sensor01");
        TF_CHECK(dev.enabled);
        TF_CHECK_NEAR(dev.temperature, 25.5f, 1e-4);
        TF_CHECK_NEAR(dev.values[0], 1.1f, 1e-4);
        TF_CHECK_NEAR(dev.values[3], 4.4f, 1e-4);

        TF_CHECK(JSON_HAS(root, "name"));
        TF_CHECK(!JSON_HAS(root, "missing"));
        JSON_GET_INT64(root, "id", i64);
        TF_CHECK_INT((int)i64, 1001);
        JSON_GET_UINT64(root, "id", u64);
        TF_CHECK_INT((int)u64, 1001);
        JSON_DELETE(root);
    }
    tf_end();

    tf_begin("json/结构体生成 JSON");
    root = JSON_CREATE();
    TF_CHECK(root != NULL);
    if (root != NULL) {
        memset(&dev, 0, sizeof(dev));
        dev.id = 7;
        strcpy(dev.name, "dev7");
        dev.enabled = false;
        dev.temperature = 1.5f;
        dev.values[0] = 0.5f;

        TF_CHECK(JSON_ADD_INT_NAME(root, dev.id, "id") != NULL);
        TF_CHECK(JSON_ADD_STRING_NAME(root, dev.name, "name") != NULL);
        TF_CHECK(JSON_ADD_BOOL_NAME(root, dev.enabled, "enabled") != NULL);
        TF_CHECK(JSON_ADD_FLOAT_NAME(root, dev.temperature, "temperature") != NULL);
        JSON_ADD_ARRAY_FLOAT_NAME(root, dev.values, "values", 4);

        output = JSON_PRINT(root);
        TF_CHECK(output != NULL);
        if (output != NULL) {
            TF_CHECK(strstr(output, "\"id\":7") != NULL);
            TF_CHECK(strstr(output, "\"enabled\":false") != NULL);
            JSON_FREE_STRING(output);
        }
        JSON_DELETE(root);
    }
    tf_end();

    tf_begin("json/默认值、别名与数组");
    root = JSON_PARSE_TEXT(
        "{\"a\":1,\"b\":\"x\",\"d\":2.5,\"arr\":[true,false]}");
    TF_CHECK(root != NULL);
    if (root != NULL) {
        int a = 0;
        char b[8] = {0};
        double d = 0.0;
        bool arr[2] = {false, false};
        int count = 0;
        int missing = 42;
        cJSON *detached;

        JSON_PARSE_INT_NAME(root, a, "a");
        JSON_PARSE_STRING_NAME(root, b, "b", sizeof(b));
        JSON_PARSE_DOUBLE(root, d);
        JSON_PARSE_ARRAY_BOOL(root, arr, 2);
        JSON_ARRAY_SIZE(root, "arr", count);
        JSON_PARSE_INT_DEFAULT(root, missing, 42);

        TF_CHECK_INT(a, 1);
        TF_CHECK_STR(b, "x");
        TF_CHECK_NEAR(d, 2.5, 1e-9);
        TF_CHECK(arr[0]);
        TF_CHECK(!arr[1]);
        TF_CHECK_INT(count, 2);
        TF_CHECK_INT(missing, 42);

        detached = JSON_DETACH(root, "a");
        TF_CHECK(detached != NULL);
        TF_CHECK(!JSON_HAS(root, "a"));
        JSON_DELETE(detached);
        JSON_DELETE(root);
    }
    tf_end();

    tf_begin("json/bool 与 double 数组序列化");
    root = JSON_CREATE();
    TF_CHECK(root != NULL);
    if (root != NULL) {
        bool flags[2] = {true, false};
        double doubles[2] = {1.25, 2.5};

        JSON_ADD_ARRAY_BOOL(root, flags, 2);
        JSON_ADD_ARRAY_DOUBLE(root, doubles, 2);
        output = JSON_PRINT(root);
        TF_CHECK(output != NULL);
        if (output != NULL) {
            TF_CHECK(strstr(output, "\"flags\":[true,false]") != NULL);
            TF_CHECK(strstr(output, "\"doubles\":[1.25,2.5]") != NULL);
            JSON_FREE_STRING(output);
        }
        JSON_DELETE(root);
    }
    tf_end();
}
