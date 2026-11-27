/*
 * var_json_test.c - var_t 与 JSON 互转测试
 *
 * 覆盖标量序列化、数组/对象序列化、文本反序列化、转义、
 * 序列化-解析往返与解析边界。
 */

#include "test.h"
#include "var_json.h"

/* 序列化后再解析回来，用于往返一致性检查 */
static bool roundtrip(const var_t *in, var_t *out, char *buf, uint32_t cap)
{
    char *text = var_to_json(in);
    bool ok;

    if (text == NULL) {
        return false;
    }
    ok = var_from_json(text, out, buf, cap);
    var_free_json(text);
    return ok;
}

void ut_var_json(void)
{
    var_t v;
    var_t out;
    var_t elem[2];
    vec_t arr;
    map_t obj;
    char buf[16];
    char *s;

    tf_begin("var_json/标量序列化");
    var_init(&v);
    s = var_to_json(&v);
    TF_CHECK_STR(s, "null");
    var_free_json(s);

    var_set_bool(&v, true);
    s = var_to_json(&v);
    TF_CHECK_STR(s, "true");
    var_free_json(s);

    var_set_int32(&v, -42);
    s = var_to_json(&v);
    TF_CHECK_STR(s, "-42");
    var_free_json(s);

    var_set_float32(&v, 1.5f);
    s = var_to_json(&v);
    TF_CHECK_STR(s, "1.5");
    var_free_json(s);

    var_set_string(&v, "abc");
    s = var_to_json(&v);
    TF_CHECK_STR(s, "\"abc\"");
    var_free_json(s);

    TF_CHECK(var_to_json(NULL) == NULL);
    tf_end();

    tf_begin("var_json/数组与对象");
    var_set_int32(&elem[0], 1);
    var_set_string(&elem[1], "x");
    memset(&arr, 0, sizeof(arr));
    arr.data = (uint8_t *)elem;
    arr.elem_size = (uint32_t)sizeof(var_t);
    arr.len = 2U;
    arr.cap = 2U;
    var_set_array(&v, &arr);
    s = var_to_json(&v);
    TF_CHECK_STR(s, "[1,\"x\"]");
    var_free_json(s);

    memset(&obj, 0, sizeof(obj));       /* 空对象无需 map_init */
    var_set_object(&v, &obj);
    s = var_to_json(&v);
    TF_CHECK_STR(s, "{}");
    var_free_json(s);
    tf_end();

    tf_begin("var_json/文本反序列化");
    TF_CHECK(var_from_json("null", &v, NULL, 0U));
    TF_CHECK(v.type == VAR_NONE);
    TF_CHECK(var_from_json("true", &v, NULL, 0U));
    TF_CHECK(v.type == VAR_BOOL);
    TF_CHECK(v.value.b);
    TF_CHECK(var_from_json("-7", &v, NULL, 0U));
    TF_CHECK(v.type == VAR_INT32);
    TF_CHECK_INT(v.value.i32, -7);
    TF_CHECK(var_from_json("2.5", &v, NULL, 0U));
    TF_CHECK(v.type == VAR_FLOAT32);

    buf[0] = '\0';
    TF_CHECK(var_from_json("\"hello\"", &v, buf, sizeof(buf)));
    TF_CHECK(v.type == VAR_STRING);
    TF_CHECK_STR(var_get_string(&v), "hello");
    TF_CHECK(var_get_string(&v) == buf);

    TF_CHECK(!var_from_json("\"x\"", &v, NULL, 0U));   /* 无缓冲不接受字符串 */
    TF_CHECK(!var_from_json("[1,2]", &v, NULL, 0U));   /* 数组不支持 */
    TF_CHECK(!var_from_json("{}", &v, NULL, 0U));      /* 对象不支持 */
    TF_CHECK(!var_from_json("bad json", &v, NULL, 0U));
    tf_end();

    tf_begin("var_json/转义");
    var_set_string(&v, "a\"b\n");
    s = var_to_json(&v);
    TF_CHECK_STR(s, "\"a\\\"b\\n\"");
    var_free_json(s);

    buf[0] = '\0';
    TF_CHECK(var_from_json("\"A\\u4E2D\"", &v, buf, sizeof(buf)));
    TF_CHECK_STR(var_get_string(&v), "A\xE4\xB8\xAD");  /* 中 = U+4E2D */
    TF_CHECK_INT((int)strlen(var_get_string(&v)), 4);
    tf_end();

    tf_begin("var_json/往返");
    var_set_int32(&v, INT32_MIN);
    TF_CHECK(roundtrip(&v, &out, buf, sizeof(buf)));
    TF_CHECK(out.type == VAR_INT32);
    TF_CHECK(out.value.i32 == INT32_MIN);

    var_set_int32(&v, INT32_MAX);
    TF_CHECK(roundtrip(&v, &out, buf, sizeof(buf)));
    TF_CHECK(out.type == VAR_INT32);
    TF_CHECK(out.value.i32 == INT32_MAX);

    var_set_float32(&v, 3.25f);
    TF_CHECK(roundtrip(&v, &out, buf, sizeof(buf)));
    TF_CHECK(out.type == VAR_FLOAT32);
    TF_CHECK(out.value.f32 == 3.25f);

    var_set_bool(&v, false);
    TF_CHECK(roundtrip(&v, &out, buf, sizeof(buf)));
    TF_CHECK(out.type == VAR_BOOL);
    TF_CHECK(out.value.b == false);

    var_init(&v);
    TF_CHECK(roundtrip(&v, &out, buf, sizeof(buf)));
    TF_CHECK(out.type == VAR_NONE);

    var_set_string(&v, "a\"b\\c\n");
    TF_CHECK(roundtrip(&v, &out, buf, sizeof(buf)));
    TF_CHECK(out.type == VAR_STRING);
    TF_CHECK_STR(out.value.s, "a\"b\\c\n");
    tf_end();

    tf_begin("var_json/解析边界");
    TF_CHECK(!var_from_json("1 2", &v, NULL, 0U));        /* 多个值 */
    TF_CHECK(!var_from_json("-", &v, NULL, 0U));          /* 缺数字 */
    TF_CHECK(!var_from_json("1e", &v, NULL, 0U));         /* 缺指数 */
    TF_CHECK(!var_from_json("\"abc", &v, buf, sizeof(buf)));  /* 未闭合 */
    TF_CHECK(!var_from_json("", &v, NULL, 0U));           /* 空输入 */

    TF_CHECK(var_from_json("3000000000", &v, NULL, 0U));  /* 超 int32 -> 浮点 */
    TF_CHECK(v.type == VAR_FLOAT32);
    TF_CHECK(var_from_json("-2147483648", &v, NULL, 0U));
    TF_CHECK(v.type == VAR_INT32);
    TF_CHECK(v.value.i32 == INT32_MIN);

    buf[0] = 'x';
    TF_CHECK(var_from_json("\"\"", &v, buf, sizeof(buf))); /* 空字符串 */
    TF_CHECK(var_get_string(&v) == buf);
    TF_CHECK_INT(buf[0], 0);
    tf_end();
}
