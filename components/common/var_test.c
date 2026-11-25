/*
 * var_test.c - 通用值容器测试
 *
 * 覆盖空值、各类型读写、类型不匹配、引用类型 NULL 清空与空指针安全。
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
    int i = 0;
    float f = 0.0f;

    memset(&arr, 0, sizeof(arr));
    memset(&obj, 0, sizeof(obj));

    tf_begin("var/空值");
    var_init(&v);
    TF_CHECK(v.type == VAR_NONE);
    TF_CHECK(var_is_none(&v));
    TF_CHECK(var_is_none(NULL));
    TF_CHECK(!var_get_int(&v, &i));
    TF_CHECK(var_get_string(&v) == NULL);
    TF_CHECK(var_get_array(&v) == NULL);
    TF_CHECK(var_get_object(&v) == NULL);
    tf_end();

    tf_begin("var/布尔");
    var_set_bool(&v, true);
    TF_CHECK(v.type == VAR_BOOL);
    TF_CHECK(var_get_bool(&v, &b));
    TF_CHECK(b);
    TF_CHECK(!var_get_int(&v, &i));             /* 类型不匹配 */
    tf_end();

    tf_begin("var/整数");
    var_set_int(&v, -123);
    TF_CHECK(v.type == VAR_INT);
    TF_CHECK(var_get_int(&v, &i));
    TF_CHECK_INT(i, -123);
    TF_CHECK(!var_get_float(&v, &f));
    tf_end();

    tf_begin("var/浮点");
    var_set_float(&v, 3.5f);
    TF_CHECK(v.type == VAR_FLOAT);
    TF_CHECK(var_get_float(&v, &f));
    TF_CHECK_NEAR(f, 3.5, 0.0001);
    TF_CHECK(!var_get_int(&v, &i));
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
    TF_CHECK(!var_get_int(&v, &i));
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

    tf_begin("var/空指针安全");
    var_init(NULL);
    var_set_int(NULL, 1);
    var_set_array(NULL, &arr);
    var_set_object(NULL, &obj);
    TF_CHECK(var_get_bool(NULL, &b) == false);
    TF_CHECK(var_get_string(NULL) == NULL);
    TF_CHECK(var_get_array(NULL) == NULL);
    TF_CHECK(var_get_object(NULL) == NULL);
    TF_CHECK(!var_get_int(&v, NULL));
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
            var_set_int(&items[0], 1);
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
