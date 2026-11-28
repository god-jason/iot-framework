/*
 * params_test.c - 参数表测试（对应 params.c）
 *
 * 覆盖：注册与重名/范围校验、读写与越界拒绝、序列化/反序列化、
 *       单参数与全部复位、非法文本。
 */

#include "test.h"
#include "params.h"

void ut_params(void)
{
    param_store_t store;
    char buf[160];
    float v = 0.0f;
    int n;

    tf_begin("params/注册与校验");

    params_init(&store);
    TF_CHECK_INT(store.count, 0);

    TF_CHECK_INT(params_add(&store, "kp", 1.0f, 0.0f, 10.0f), 0);
    TF_CHECK_INT(params_add(&store, "ki", 0.5f, 0.0f, 5.0f), 0);
    TF_CHECK_INT(params_add(&store, "kp", 2.0f, 0.0f, 10.0f), -1);  /* 重名 */
    TF_CHECK_INT(params_add(&store, "bad", 5.0f, 0.0f, 1.0f), -1);  /* 默认越界 */
    TF_CHECK_INT(params_add(&store, NULL, 0.0f, 0.0f, 1.0f), -1);
    TF_CHECK_INT(params_add(&store, "", 0.0f, 0.0f, 1.0f), -1);
    TF_CHECK_INT(params_add(&store, "name_is_way_too_long_for_store",
                            0.0f, 0.0f, 1.0f), -1);
    TF_CHECK_INT(store.count, 2);

    tf_end();

    tf_begin("params/读写与越界拒绝");

    TF_CHECK_INT(params_get(&store, "kp", &v), 0);
    TF_CHECK_NEAR(v, 1.0f, 1e-6);          /* 初始为默认值 */

    TF_CHECK_INT(params_set(&store, "kp", 5.5f), 0);
    TF_CHECK_INT(params_get(&store, "kp", &v), 0);
    TF_CHECK_NEAR(v, 5.5f, 1e-6);

    TF_CHECK_INT(params_set(&store, "kp", 11.0f), -1);
    TF_CHECK_INT(params_set(&store, "kp", -1.0f), -1);
    TF_CHECK_INT(params_set(&store, "nope", 1.0f), -1);
    TF_CHECK_INT(params_get(&store, "nope", &v), -1);
    TF_CHECK_INT(params_get(&store, "kp", NULL), -1);

    tf_end();

    tf_begin("params/序列化与反序列化");

    n = params_serialize(&store, buf, sizeof(buf));
    TF_CHECK(n > 0);
    TF_CHECK(strstr(buf, "kp=5.5") != NULL);
    TF_CHECK(strstr(buf, "ki=0.5") != NULL);

    TF_CHECK_INT(params_set(&store, "kp", 2.0f), 0);
    TF_CHECK_INT(params_deserialize(&store, buf), 2);
    TF_CHECK_INT(params_get(&store, "kp", &v), 0);
    TF_CHECK_NEAR(v, 5.5f, 1e-4);

    /* 注释与空行被忽略，未知参数被跳过 */
    TF_CHECK_INT(params_set(&store, "kp", 1.0f), 0);
    TF_CHECK_INT(params_deserialize(&store, "# comment\n\nkp=3.5\nunknown=1\n"), 1);
    TF_CHECK_INT(params_get(&store, "kp", &v), 0);
    TF_CHECK_NEAR(v, 3.5f, 1e-4);

    TF_CHECK_INT(params_serialize(&store, buf, 4U), -1);
    TF_CHECK_INT(params_serialize(NULL, buf, sizeof(buf)), -1);
    TF_CHECK_INT(params_deserialize(&store, ""), -1);
    TF_CHECK_INT(params_deserialize(&store, NULL), -1);
    TF_CHECK_INT(params_deserialize(&store, "kp=abc\n"), -1);

    tf_end();

    tf_begin("params/恢复默认值");

    TF_CHECK_INT(params_set(&store, "kp", 9.0f), 0);
    TF_CHECK_INT(params_reset(&store, "kp"), 0);
    TF_CHECK_INT(params_get(&store, "kp", &v), 0);
    TF_CHECK_NEAR(v, 1.0f, 1e-6);
    TF_CHECK_INT(params_reset(&store, "nope"), -1);

    TF_CHECK_INT(params_set(&store, "ki", 3.0f), 0);
    params_reset_all(&store);
    TF_CHECK_INT(params_get(&store, "ki", &v), 0);
    TF_CHECK_NEAR(v, 0.5f, 1e-6);

    tf_end();
}
