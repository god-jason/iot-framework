/* cJSON 单元测试：版本、构造、解析/打印、数组/对象操作、比较与工具函数。 */
#include "test_framework.h"
#include "cJSON.h"

#include <string.h>
#include <stdbool.h>

static void free_printed(char *p)
{
    if (p != NULL) {
        cJSON_free(p);
    }
}

static void test_version(void)
{
    tf_begin("cJSON: 版本号");
    TF_CHECK_INT(CJSON_VERSION_MAJOR, 1);
    TF_CHECK_INT(CJSON_VERSION_MINOR, 7);
    TF_CHECK_INT(CJSON_VERSION_PATCH, 19);
    TF_CHECK_STR(cJSON_Version(), "1.7.19");
    tf_end();
}

static void test_create(void)
{
    cJSON *item;
    cJSON *str;
    double nan_value;

    tf_begin("cJSON: 创建基础类型");
    item = cJSON_CreateNull();
    TF_CHECK(item != NULL);
    TF_CHECK(cJSON_IsNull(item) == true);
    TF_CHECK(cJSON_IsInvalid(item) == false);
    cJSON_Delete(item);

    item = cJSON_CreateTrue();
    TF_CHECK(cJSON_IsTrue(item) == true);
    TF_CHECK(cJSON_IsBool(item) == true);
    TF_CHECK(cJSON_IsFalse(item) == false);
    cJSON_Delete(item);

    item = cJSON_CreateFalse();
    TF_CHECK(cJSON_IsFalse(item) == true);
    TF_CHECK(cJSON_IsBool(item) == true);
    cJSON_Delete(item);

    item = cJSON_CreateBool(1);
    TF_CHECK(cJSON_IsTrue(item) == true);
    cJSON_Delete(item);

    item = cJSON_CreateNumber(3.5);
    TF_CHECK(cJSON_IsNumber(item) == true);
    TF_CHECK_NEAR(cJSON_GetNumberValue(item), 3.5, 1e-9);
    cJSON_Delete(item);

    str = cJSON_CreateString("abc");
    TF_CHECK(cJSON_IsString(str) == true);
    TF_CHECK_STR(cJSON_GetStringValue(str), "abc");
    nan_value = cJSON_GetNumberValue(str);
    TF_CHECK(nan_value != nan_value); /* 非数字返回 NaN */
    cJSON_Delete(str);

    item = cJSON_CreateRaw("[1,2]");
    TF_CHECK(cJSON_IsRaw(item) == true);
    TF_CHECK(cJSON_IsString(item) == false);
    TF_CHECK(cJSON_GetStringValue(item) == NULL);
    TF_CHECK_STR(item->valuestring, "[1,2]");
    cJSON_Delete(item);

    item = cJSON_CreateArray();
    TF_CHECK(cJSON_IsArray(item) == true);
    TF_CHECK_INT(cJSON_GetArraySize(item), 0);
    cJSON_Delete(item);

    item = cJSON_CreateObject();
    TF_CHECK(cJSON_IsObject(item) == true);
    TF_CHECK_INT(cJSON_GetArraySize(item), 0);
    cJSON_Delete(item);
    tf_end();

    tf_begin("cJSON: 创建非法参数与空指针谓词");
    TF_CHECK(cJSON_CreateString(NULL) == NULL);
    TF_CHECK(cJSON_CreateRaw(NULL) == NULL);
    TF_CHECK(cJSON_GetStringValue(NULL) == NULL);
    TF_CHECK(cJSON_IsInvalid(NULL) == false);
    TF_CHECK(cJSON_IsNull(NULL) == false);
    TF_CHECK(cJSON_IsNumber(NULL) == false);
    TF_CHECK(cJSON_IsString(NULL) == false);
    TF_CHECK(cJSON_IsArray(NULL) == false);
    TF_CHECK(cJSON_IsObject(NULL) == false);
    TF_CHECK(cJSON_IsRaw(NULL) == false);
    TF_CHECK_INT(cJSON_GetArraySize(NULL), 0);
    TF_CHECK(cJSON_GetArrayItem(NULL, 0) == NULL);
    tf_end();
}

static void test_parse(void)
{
    static const char *json =
        "{\"name\":\"iot\",\"value\":42,\"ok\":true,"
        "\"none\":null,\"arr\":[1,2,3],\"nested\":{\"x\":1}}";
    cJSON *root;
    cJSON *item;

    tf_begin("cJSON: 解析对象与字段访问");
    root = cJSON_Parse(json);
    TF_CHECK(root != NULL);
    TF_CHECK(cJSON_IsObject(root) == true);
    TF_CHECK_INT(cJSON_GetArraySize(root), 6);

    item = cJSON_GetObjectItem(root, "name");
    TF_CHECK(item != NULL);
    TF_CHECK_STR(cJSON_GetStringValue(item), "iot");

    item = cJSON_GetObjectItem(root, "NAME"); /* 默认大小写不敏感 */
    TF_CHECK(item != NULL);
    TF_CHECK(cJSON_GetObjectItemCaseSensitive(root, "NAME") == NULL);
    TF_CHECK(cJSON_HasObjectItem(root, "value") == true);
    TF_CHECK(cJSON_HasObjectItem(root, "missing") == false);

    item = cJSON_GetObjectItem(root, "value");
    TF_CHECK(cJSON_IsNumber(item) == true);
    TF_CHECK_NEAR(cJSON_GetNumberValue(item), 42.0, 1e-9);

    TF_CHECK(cJSON_IsTrue(cJSON_GetObjectItem(root, "ok")) == true);
    TF_CHECK(cJSON_IsNull(cJSON_GetObjectItem(root, "none")) == true);

    item = cJSON_GetObjectItem(root, "arr");
    TF_CHECK(cJSON_IsArray(item) == true);
    TF_CHECK_INT(cJSON_GetArraySize(item), 3);
    TF_CHECK_NEAR(cJSON_GetNumberValue(cJSON_GetArrayItem(item, 0)), 1.0, 1e-9);
    TF_CHECK_NEAR(cJSON_GetNumberValue(cJSON_GetArrayItem(item, 2)), 3.0, 1e-9);
    TF_CHECK(cJSON_GetArrayItem(item, -1) == NULL);
    TF_CHECK(cJSON_GetArrayItem(item, 3) == NULL);

    item = cJSON_GetObjectItem(root, "nested");
    TF_CHECK(cJSON_IsObject(item) == true);
    TF_CHECK_NEAR(cJSON_GetNumberValue(cJSON_GetObjectItem(item, "x")), 1.0, 1e-9);
    cJSON_Delete(root);
    tf_end();

    tf_begin("cJSON: 解析根值类型");
    root = cJSON_Parse("null");
    TF_CHECK(root != NULL && cJSON_IsNull(root) == true);
    cJSON_Delete(root);

    root = cJSON_Parse("true");
    TF_CHECK(root != NULL && cJSON_IsTrue(root) == true);
    cJSON_Delete(root);

    root = cJSON_Parse("false");
    TF_CHECK(root != NULL && cJSON_IsFalse(root) == true);
    cJSON_Delete(root);

    root = cJSON_Parse("-12.5");
    TF_CHECK(root != NULL && cJSON_IsNumber(root) == true);
    TF_CHECK_NEAR(cJSON_GetNumberValue(root), -12.5, 1e-9);
    cJSON_Delete(root);

    root = cJSON_Parse("1e3");
    TF_CHECK(root != NULL && cJSON_IsNumber(root) == true);
    TF_CHECK_NEAR(cJSON_GetNumberValue(root), 1000.0, 1e-9);
    cJSON_Delete(root);

    root = cJSON_Parse("{}");
    TF_CHECK(root != NULL && cJSON_IsObject(root) == true);
    TF_CHECK_INT(cJSON_GetArraySize(root), 0);
    cJSON_Delete(root);

    root = cJSON_Parse("[]");
    TF_CHECK(root != NULL && cJSON_IsArray(root) == true);
    TF_CHECK_INT(cJSON_GetArraySize(root), 0);
    cJSON_Delete(root);
    tf_end();
}

static void test_parse_errors(void)
{
    const char *end = NULL;
    cJSON *root;

    tf_begin("cJSON: 解析失败路径");
    TF_CHECK(cJSON_Parse(NULL) == NULL);
    TF_CHECK(cJSON_Parse("") == NULL);
    TF_CHECK(cJSON_Parse("{") == NULL);
    TF_CHECK(cJSON_Parse("{\"a\":}") == NULL);
    TF_CHECK(cJSON_Parse("not json") == NULL);
    TF_CHECK(cJSON_ParseWithLength(NULL, 4) == NULL);
    TF_CHECK(cJSON_ParseWithLength("{}", 0) == NULL);
    TF_CHECK(cJSON_GetErrorPtr() != NULL);
    tf_end();

    tf_begin("cJSON: ParseWithLength 只解析指定长度");
    root = cJSON_ParseWithLength("{\"a\":1}XYZ", 7);
    TF_CHECK(root != NULL);
    TF_CHECK(cJSON_HasObjectItem(root, "a") == true);
    cJSON_Delete(root);
    tf_end();

    tf_begin("cJSON: ParseWithOpts 尾随内容检查");
    root = cJSON_ParseWithOpts("{}garbage", &end, 0);
    TF_CHECK(root != NULL);
    TF_CHECK(end != NULL && *end == 'g');
    cJSON_Delete(root);

    root = cJSON_ParseWithOpts("{}garbage", &end, 1);
    TF_CHECK(root == NULL);

    root = cJSON_ParseWithLengthOpts("{\"a\":1}XYZ", 7, &end, 1);
    TF_CHECK(root == NULL); /* 指定长度内没有 NUL 终止符 */

    root = cJSON_ParseWithOpts("{}", NULL, 1);
    TF_CHECK(root != NULL);
    cJSON_Delete(root);
    tf_end();
}

static void test_print(void)
{
    cJSON *obj;
    char *unformatted;
    char *formatted;
    char *buffered;
    char preallocated[128];

    tf_begin("cJSON: Print / PrintUnformatted / PrintBuffered");
    obj = cJSON_CreateObject();
    TF_CHECK(obj != NULL);
    TF_CHECK(cJSON_AddStringToObject(obj, "a", "abc") != NULL);
    TF_CHECK(cJSON_AddNumberToObject(obj, "n", 1) != NULL);

    unformatted = cJSON_PrintUnformatted(obj);
    TF_CHECK(unformatted != NULL);
    TF_CHECK_STR(unformatted, "{\"a\":\"abc\",\"n\":1}");

    formatted = cJSON_Print(obj);
    TF_CHECK(formatted != NULL);
    TF_CHECK(strlen(formatted) > strlen(unformatted));
    TF_CHECK(strchr(formatted, '\n') != NULL);

    buffered = cJSON_PrintBuffered(obj, 1, 0);
    TF_CHECK(buffered != NULL);
    TF_CHECK_STR(buffered, unformatted);

    TF_CHECK(cJSON_Print(NULL) == NULL);
    TF_CHECK(cJSON_PrintUnformatted(NULL) == NULL);

    free_printed(unformatted);
    free_printed(formatted);
    free_printed(buffered);
    cJSON_Delete(obj);
    tf_end();

    tf_begin("cJSON: PrintPreallocated 边界");
    obj = cJSON_CreateNumber(123);
    TF_CHECK_INT(cJSON_PrintPreallocated(obj, preallocated, (int)sizeof(preallocated), 0), 1);
    TF_CHECK_STR(preallocated, "123");

    TF_CHECK_INT(cJSON_PrintPreallocated(obj, preallocated, 2, 0), 0);
    TF_CHECK_INT(cJSON_PrintPreallocated(obj, NULL, (int)sizeof(preallocated), 0), 0);
    TF_CHECK_INT(cJSON_PrintPreallocated(obj, preallocated, -1, 0), 0);
    cJSON_Delete(obj);
    tf_end();
}

static void test_array_ops(void)
{
    cJSON *arr;
    cJSON *item;

    tf_begin("cJSON: 数组添加/索引/插入/替换/移除");
    arr = cJSON_CreateArray();
    TF_CHECK(cJSON_AddItemToArray(arr, cJSON_CreateNumber(10)) == 1);
    TF_CHECK(cJSON_AddItemToArray(arr, cJSON_CreateNumber(20)) == 1);
    TF_CHECK(cJSON_AddItemToArray(arr, cJSON_CreateNumber(30)) == 1);
    TF_CHECK_INT(cJSON_GetArraySize(arr), 3);
    TF_CHECK_NEAR(cJSON_GetNumberValue(cJSON_GetArrayItem(arr, 1)), 20.0, 1e-9);

    TF_CHECK(cJSON_InsertItemInArray(arr, 1, cJSON_CreateNumber(15)) == 1);
    TF_CHECK_INT(cJSON_GetArraySize(arr), 4);
    TF_CHECK_NEAR(cJSON_GetNumberValue(cJSON_GetArrayItem(arr, 1)), 15.0, 1e-9);
    TF_CHECK_NEAR(cJSON_GetNumberValue(cJSON_GetArrayItem(arr, 2)), 20.0, 1e-9);

    TF_CHECK(cJSON_ReplaceItemInArray(arr, 0, cJSON_CreateNumber(11)) == 1);
    TF_CHECK_NEAR(cJSON_GetNumberValue(cJSON_GetArrayItem(arr, 0)), 11.0, 1e-9);

    item = cJSON_DetachItemFromArray(arr, 3);
    TF_CHECK(item != NULL);
    TF_CHECK_NEAR(cJSON_GetNumberValue(item), 30.0, 1e-9);
    cJSON_Delete(item);
    TF_CHECK_INT(cJSON_GetArraySize(arr), 3);

    cJSON_DeleteItemFromArray(arr, 0);
    TF_CHECK_INT(cJSON_GetArraySize(arr), 2);

    item = cJSON_CreateNumber(1);
    TF_CHECK(cJSON_AddItemToArray(NULL, item) == 0);
    cJSON_Delete(item);
    TF_CHECK(cJSON_AddItemToArray(arr, NULL) == 0);
    cJSON_Delete(arr);
    tf_end();

    tf_begin("cJSON: 数组引用不影响原对象");
    {
        cJSON *num = cJSON_CreateNumber(5);
        arr = cJSON_CreateArray();
        TF_CHECK(cJSON_AddItemReferenceToArray(arr, num) == 1);
        TF_CHECK(cJSON_AddItemReferenceToArray(arr, num) == 1);
        TF_CHECK_INT(cJSON_GetArraySize(arr), 2);
        cJSON_Delete(arr);
        TF_CHECK_NEAR(cJSON_GetNumberValue(num), 5.0, 1e-9);
        cJSON_Delete(num);
    }
    tf_end();
}

static void test_object_ops(void)
{
    cJSON *obj;
    cJSON *item;
    cJSON *detached;
    cJSON *new_item;

    tf_begin("cJSON: 对象字段添加与类型");
    obj = cJSON_CreateObject();
    TF_CHECK(cJSON_AddStringToObject(obj, "s", "v") != NULL);
    TF_CHECK(cJSON_AddNumberToObject(obj, "n", 1) != NULL);
    TF_CHECK(cJSON_AddBoolToObject(obj, "b", 1) != NULL);
    TF_CHECK(cJSON_AddNullToObject(obj, "z") != NULL);
    TF_CHECK(cJSON_AddArrayToObject(obj, "a") != NULL);
    TF_CHECK(cJSON_AddObjectToObject(obj, "o") != NULL);
    TF_CHECK(cJSON_AddRawToObject(obj, "r", "[1]") != NULL);
    TF_CHECK_INT(cJSON_GetArraySize(obj), 7);

    TF_CHECK(cJSON_HasObjectItem(obj, "s") == true);
    TF_CHECK(cJSON_IsTrue(cJSON_GetObjectItem(obj, "b")) == true);
    TF_CHECK(cJSON_IsNull(cJSON_GetObjectItem(obj, "z")) == true);
    TF_CHECK(cJSON_IsArray(cJSON_GetObjectItem(obj, "a")) == true);
    TF_CHECK(cJSON_IsObject(cJSON_GetObjectItem(obj, "o")) == true);
    TF_CHECK(cJSON_IsRaw(cJSON_GetObjectItem(obj, "r")) == true);
    TF_CHECK_STR(cJSON_GetObjectItem(obj, "s")->valuestring, "v");

    TF_CHECK(cJSON_AddItemToObject(obj, "x", cJSON_CreateNumber(2)) == 1);
    TF_CHECK(cJSON_AddItemToObjectCS(obj, "cs", cJSON_CreateNumber(3)) == 1);
    TF_CHECK(cJSON_HasObjectItem(obj, "cs") == true);

    item = cJSON_CreateNumber(9);
    TF_CHECK(cJSON_AddItemToObject(obj, NULL, item) == 0);
    cJSON_Delete(item);
    tf_end();

    tf_begin("cJSON: 对象字段查找/替换/移除");
    item = cJSON_GetObjectItemCaseSensitive(obj, "S");
    TF_CHECK(item == NULL);
    TF_CHECK(cJSON_GetObjectItemCaseSensitive(obj, "s") != NULL);

    TF_CHECK(cJSON_ReplaceItemInObject(obj, "s", cJSON_CreateString("w")) == 1);
    TF_CHECK_STR(cJSON_GetObjectItem(obj, "s")->valuestring, "w");

    new_item = cJSON_CreateNumber(7);
    TF_CHECK(cJSON_ReplaceItemInObject(obj, "missing", new_item) == 0);
    cJSON_Delete(new_item);

    detached = cJSON_DetachItemFromObject(obj, "x");
    TF_CHECK(detached != NULL);
    TF_CHECK_NEAR(cJSON_GetNumberValue(detached), 2.0, 1e-9);
    cJSON_Delete(detached);
    TF_CHECK(cJSON_HasObjectItem(obj, "x") == false);

    TF_CHECK(cJSON_DetachItemFromObjectCaseSensitive(obj, "S") == NULL);

    cJSON_DeleteItemFromObject(obj, "n");
    TF_CHECK(cJSON_HasObjectItem(obj, "n") == false);
    cJSON_DeleteItemFromObjectCaseSensitive(obj, "o");
    TF_CHECK(cJSON_HasObjectItem(obj, "o") == false);

    cJSON_Delete(obj);
    tf_end();
}

static void test_duplicate_compare(void)
{
    cJSON *root;
    cJSON *dup;
    cJSON *shallow;
    cJSON *a;
    cJSON *b;

    tf_begin("cJSON: Duplicate 深/浅拷贝");
    root = cJSON_Parse("[1,\"two\",{\"three\":3}]");
    TF_CHECK(root != NULL);

    dup = cJSON_Duplicate(root, 1);
    TF_CHECK(dup != NULL);
    TF_CHECK(cJSON_Compare(root, dup, 1) == true);
    TF_CHECK_INT(cJSON_GetArraySize(dup), 3);
    TF_CHECK_STR(cJSON_GetStringValue(cJSON_GetArrayItem(dup, 1)), "two");

    shallow = cJSON_Duplicate(root, 0);
    TF_CHECK(shallow != NULL);
    TF_CHECK(cJSON_IsArray(shallow) == true);
    TF_CHECK_INT(cJSON_GetArraySize(shallow), 0);

    TF_CHECK(cJSON_Duplicate(NULL, 1) == NULL);

    cJSON_Delete(dup);
    cJSON_Delete(shallow);
    cJSON_Delete(root);
    tf_end();

    tf_begin("cJSON: Compare 类型与大小写");
    a = cJSON_Parse("{\"A\":1}");
    b = cJSON_Parse("{\"a\":1}");
    TF_CHECK(cJSON_Compare(a, b, 0) == true);
    TF_CHECK(cJSON_Compare(a, b, 1) == false);
    TF_CHECK(cJSON_Compare(a, NULL, 1) == false);
    TF_CHECK(cJSON_Compare(NULL, b, 1) == false);
    cJSON_Delete(a);
    cJSON_Delete(b);

    a = cJSON_Parse("[1,2]");
    b = cJSON_Parse("[2,1]");
    TF_CHECK(cJSON_Compare(a, b, 1) == false);
    cJSON_Delete(a);
    cJSON_Delete(b);

    a = cJSON_CreateNumber(1);
    b = cJSON_CreateNumber(1);
    TF_CHECK(cJSON_Compare(a, b, 1) == true);
    cJSON_Delete(a);
    cJSON_Delete(b);
    tf_end();
}

static void test_set_values(void)
{
    cJSON *str;
    cJSON *num;
    cJSON *boolean;

    tf_begin("cJSON: SetValuestring");
    str = cJSON_CreateString("abc");
    TF_CHECK_STR(cJSON_SetValuestring(str, "abcdef"), "abcdef");
    TF_CHECK_STR(cJSON_GetStringValue(str), "abcdef");
    TF_CHECK_STR(cJSON_SetValuestring(str, "x"), "x");
    TF_CHECK(cJSON_SetValuestring(str, NULL) == NULL);
    TF_CHECK_STR(cJSON_GetStringValue(str), "x");
    cJSON_Delete(str);

    num = cJSON_CreateNumber(1);
    TF_CHECK(cJSON_SetValuestring(num, "x") == NULL);
    cJSON_Delete(num);
    tf_end();

    tf_begin("cJSON: SetNumber / SetInt / SetBool");
    num = cJSON_CreateNumber(1);
    TF_CHECK_NEAR(cJSON_SetNumberHelper(num, 2.5), 2.5, 1e-9);
    TF_CHECK_NEAR(num->valuedouble, 2.5, 1e-9);
    TF_CHECK_INT(num->valueint, 2);
    cJSON_SetIntValue(num, 7);
    TF_CHECK_INT(num->valueint, 7);
    TF_CHECK_NEAR(num->valuedouble, 7.0, 1e-9);
    TF_CHECK_NEAR(cJSON_SetNumberValue(num, 9), 9.0, 1e-9);
    TF_CHECK_NEAR(num->valuedouble, 9.0, 1e-9);
    cJSON_Delete(num);

    boolean = cJSON_CreateTrue();
    TF_CHECK_INT(cJSON_SetBoolValue(boolean, 0), cJSON_False);
    TF_CHECK(cJSON_IsFalse(boolean) == true);
    TF_CHECK_INT(cJSON_SetBoolValue(boolean, 1), cJSON_True);
    TF_CHECK(cJSON_IsTrue(boolean) == true);
    cJSON_Delete(boolean);

    num = cJSON_CreateNumber(1);
    TF_CHECK_INT(cJSON_SetBoolValue(num, 1), cJSON_Invalid);
    TF_CHECK(cJSON_IsNumber(num) == true);
    cJSON_Delete(num);
    tf_end();
}

static void test_minify(void)
{
    char json1[] = "  {  \"a\" : \"x y\"  }  ";
    char json2[] = "{ \"a\" : 1, /*c*/ \"b\" : [ 1, 2 ] } //x";

    tf_begin("cJSON: Minify 去除空白与注释");
    cJSON_Minify(json1);
    TF_CHECK_STR(json1, "{\"a\":\"x y\"}");

    cJSON_Minify(json2);
    TF_CHECK_STR(json2, "{\"a\":1,\"b\":[1,2]}");

    cJSON_Minify(NULL);
    TF_CHECK(1);
    tf_end();
}

static void test_array_helpers(void)
{
    static const int ints[] = {2, 4, 6};
    static const float floats[] = {1.5f, 2.5f};
    static const double doubles[] = {1.25, 3.75};
    static const char *strings[] = {"x", "y"};
    cJSON *arr;

    tf_begin("cJSON: CreateIntArray / FloatArray / DoubleArray / StringArray");
    arr = cJSON_CreateIntArray(ints, 3);
    TF_CHECK(arr != NULL);
    TF_CHECK_INT(cJSON_GetArraySize(arr), 3);
    TF_CHECK_NEAR(cJSON_GetNumberValue(cJSON_GetArrayItem(arr, 0)), 2.0, 1e-9);
    TF_CHECK_NEAR(cJSON_GetNumberValue(cJSON_GetArrayItem(arr, 2)), 6.0, 1e-9);
    cJSON_Delete(arr);

    arr = cJSON_CreateFloatArray(floats, 2);
    TF_CHECK(arr != NULL);
    TF_CHECK_INT(cJSON_GetArraySize(arr), 2);
    TF_CHECK_NEAR(cJSON_GetNumberValue(cJSON_GetArrayItem(arr, 1)), 2.5, 1e-6);
    cJSON_Delete(arr);

    arr = cJSON_CreateDoubleArray(doubles, 2);
    TF_CHECK(arr != NULL);
    TF_CHECK_INT(cJSON_GetArraySize(arr), 2);
    TF_CHECK_NEAR(cJSON_GetNumberValue(cJSON_GetArrayItem(arr, 0)), 1.25, 1e-9);
    cJSON_Delete(arr);

    arr = cJSON_CreateStringArray(strings, 2);
    TF_CHECK(arr != NULL);
    TF_CHECK_INT(cJSON_GetArraySize(arr), 2);
    TF_CHECK_STR(cJSON_GetStringValue(cJSON_GetArrayItem(arr, 0)), "x");
    TF_CHECK_STR(cJSON_GetStringValue(cJSON_GetArrayItem(arr, 1)), "y");
    cJSON_Delete(arr);

    TF_CHECK(cJSON_CreateIntArray(NULL, 0) == NULL);
    TF_CHECK(cJSON_CreateIntArray(ints, -1) == NULL);
    tf_end();

    tf_begin("cJSON: 空数组辅助构造");
    arr = cJSON_CreateIntArray(ints, 0);
    TF_CHECK(arr != NULL);
    TF_CHECK(cJSON_IsArray(arr) == true);
    TF_CHECK_INT(cJSON_GetArraySize(arr), 0);
    cJSON_Delete(arr);
    tf_end();
}

static void test_malloc(void)
{
    void *p;

    tf_begin("cJSON: malloc/free 包装");
    p = cJSON_malloc(32);
    TF_CHECK(p != NULL);
    cJSON_free(p);
    cJSON_free(NULL);
    TF_CHECK(1);
    tf_end();
}

void ut_cJSON(void)
{
    test_version();
    test_create();
    test_parse();
    test_parse_errors();
    test_print();
    test_array_ops();
    test_object_ops();
    test_duplicate_compare();
    test_set_values();
    test_minify();
    test_array_helpers();
    test_malloc();
}
