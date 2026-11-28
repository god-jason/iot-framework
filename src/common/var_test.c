/*
 * var_test.c - 通用值容器测试
 *
 * 覆盖空值、各类型读写、类型不匹配、引用类型 NULL 清空、
 * 类型转换（var_to_xxx）、空指针安全与递归释放。
 */

#include "test.h"
#include "var.h"
#include "ros_mem.h"

void ut_var(void)
{
    var_t v;
    vec_t arr;
    map_t obj;
    bool b = false;
    int8_t i8 = 0;
    uint8_t u8 = 0;
    int16_t i16 = 0;
    uint16_t u16 = 0;
    int32_t i32 = 0;
    uint32_t u32 = 0;
    int64_t i64 = 0;
    uint64_t u64 = 0;
    float f32 = 0.0f;
    double f64 = 0.0;
    char text[24];

    memset(&arr, 0, sizeof(arr));
    memset(&obj, 0, sizeof(obj));

    tf_begin("var/空值");
    var_init(&v);
    TF_CHECK(v.type == VAR_NONE);
    TF_CHECK(var_is_none(&v));
    TF_CHECK(var_is_none(NULL));
    TF_CHECK(!var_get_int32(&v, &i32));
    TF_CHECK(var_get_string(&v) == NULL);
    TF_CHECK(var_get_array(&v) == NULL);
    TF_CHECK(var_get_object(&v) == NULL);
    tf_end();

    tf_begin("var/布尔");
    var_set_bool(&v, true);
    TF_CHECK(v.type == VAR_BOOL);
    TF_CHECK(var_get_bool(&v, &b));
    TF_CHECK(b);
    TF_CHECK(!var_get_int32(&v, &i32));         /* 类型不匹配 */
    tf_end();

    tf_begin("var/整数");
    var_set_int8(&v, -8);
    TF_CHECK(var_get_int8(&v, &i8) && i8 == -8);
    var_set_uint8(&v, 200);
    TF_CHECK(var_get_uint8(&v, &u8) && u8 == 200);
    var_set_int16(&v, -1600);
    TF_CHECK(var_get_int16(&v, &i16) && i16 == -1600);
    var_set_uint16(&v, 60000);
    TF_CHECK(var_get_uint16(&v, &u16) && u16 == 60000);
    var_set_int32(&v, -123456);
    TF_CHECK(v.type == VAR_INT32);
    TF_CHECK(var_get_int32(&v, &i32));
    TF_CHECK_INT(i32, -123456);
    TF_CHECK(!var_get_float32(&v, &f32));
    var_set_uint32(&v, 4000000000U);
    TF_CHECK(var_get_uint32(&v, &u32) && u32 == 4000000000U);
    var_set_int64(&v, -1234567890123LL);
    TF_CHECK(var_get_int64(&v, &i64) && i64 == -1234567890123LL);
    var_set_uint64(&v, 12345678901234567890ULL);
    TF_CHECK(var_get_uint64(&v, &u64) && u64 == 12345678901234567890ULL);
    tf_end();

    tf_begin("var/浮点");
    var_set_float32(&v, 3.5f);
    TF_CHECK(v.type == VAR_FLOAT32);
    TF_CHECK(var_get_float32(&v, &f32));
    TF_CHECK_NEAR(f32, 3.5, 0.0001);
    TF_CHECK(!var_get_int32(&v, &i32));
    var_set_float64(&v, 3.25);
    TF_CHECK(v.type == VAR_FLOAT64);
    TF_CHECK(var_get_float64(&v, &f64));
    TF_CHECK_NEAR(f64, 3.25, 0.000001);
    tf_end();

    tf_begin("var/字符串");
    var_set_string(&v, "hello");
    TF_CHECK(v.type == VAR_STRING);
    TF_CHECK_STR(var_get_string(&v), "hello");
    TF_CHECK(!var_get_bool(&v, &b));
    var_set_string(&v, NULL);                   /* NULL 视为清空 */
    TF_CHECK(var_is_none(&v));
    tf_end();

    tf_begin("var/数组");
    var_set_array(&v, &arr);
    TF_CHECK(v.type == VAR_ARRAY);
    TF_CHECK(var_get_array(&v) == &arr);
    TF_CHECK(var_get_object(&v) == NULL);
    TF_CHECK(!var_get_int32(&v, &i32));
    var_set_array(&v, NULL);
    TF_CHECK(var_is_none(&v));
    tf_end();

    tf_begin("var/对象");
    var_set_object(&v, &obj);
    TF_CHECK(v.type == VAR_OBJECT);
    TF_CHECK(var_get_object(&v) == &obj);
    TF_CHECK(var_get_array(&v) == NULL);
    TF_CHECK(var_get_string(&v) == NULL);
    var_set_object(&v, NULL);
    TF_CHECK(var_is_none(&v));
    tf_end();

    tf_begin("var/类型转换");
    var_set_int32(&v, 300);
    TF_CHECK(var_to_int16(&v, &i16) && i16 == 300);
    TF_CHECK(!var_to_int8(&v, &i8));            /* 越界 */
    TF_CHECK(var_to_float32(&v, &f32) && f32 == 300.0f);
    TF_CHECK(var_to_string(&v, text, sizeof(text)));
    TF_CHECK_STR(text, "300");

    var_set_int32(&v, -1);
    TF_CHECK(!var_to_uint32(&v, &u32));         /* 负数不能转无符号 */

    var_set_float32(&v, 2.9f);
    TF_CHECK(var_to_int32(&v, &i32) && i32 == 2);  /* 向零截断 */

    var_set_string(&v, "123");
    TF_CHECK(var_to_int32(&v, &i32) && i32 == 123);
    var_set_string(&v, "true");
    TF_CHECK(var_to_bool(&v, &b) && b);
    var_set_string(&v, "0");
    TF_CHECK(var_to_bool(&v, &b) && !b);

    var_set_uint64(&v, 9000000000ULL);
    TF_CHECK(var_to_int64(&v, &i64) && i64 == 9000000000LL);
    var_set_int64(&v, -5);
    TF_CHECK(!var_to_uint64(&v, &u64));
    tf_end();

    tf_begin("var/空指针安全");
    var_init(NULL);
    var_set_int32(NULL, 1);
    var_set_array(NULL, &arr);
    var_set_object(NULL, &obj);
    TF_CHECK(var_get_bool(NULL, &b) == false);
    TF_CHECK(var_get_string(NULL) == NULL);
    TF_CHECK(var_get_array(NULL) == NULL);
    TF_CHECK(var_get_object(NULL) == NULL);
    TF_CHECK(!var_get_int32(&v, NULL));
    TF_CHECK(!var_to_int32(NULL, &i32));
    var_free(NULL);
    tf_end();

    tf_begin("var/递归释放");
    var_set_string(&v, "borrowed");
    var_free(&v);                               /* 字符串借用，不释放 */
    TF_CHECK(var_is_none(&v));

    /* ARRAY：容器与元素由 ros_mem 分配 */
    {
        vec_t *heap_arr = (vec_t *)ros_mem_malloc(sizeof(vec_t));
        var_t *items = (var_t *)ros_mem_malloc(sizeof(var_t) * 2U);

        TF_CHECK(heap_arr != NULL && items != NULL);
        if (heap_arr != NULL && items != NULL) {
            memset(heap_arr, 0, sizeof(*heap_arr));
            memset(items, 0, sizeof(var_t) * 2U);
            var_set_int32(&items[0], 1);
            var_set_string(&items[1], "x");
            heap_arr->data = (uint8_t *)items;
            heap_arr->elem_size = (uint32_t)sizeof(var_t);
            heap_arr->len = 2U;
            heap_arr->cap = 2U;
            var_set_array(&v, heap_arr);
            var_free(&v);
            TF_CHECK(var_is_none(&v));
        }
    }

    /* OBJECT：容器由 ros_mem 分配 */
    {
        map_t *heap_obj = (map_t *)ros_mem_malloc(sizeof(map_t));

        TF_CHECK(heap_obj != NULL);
        if (heap_obj != NULL) {
            memset(heap_obj, 0, sizeof(*heap_obj));
            var_set_object(&v, heap_obj);
            var_free(&v);
            TF_CHECK(var_is_none(&v));
        }
    }
    tf_end();
}
