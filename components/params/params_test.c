/* params 单元测试：注册、范围校验、序列化与恢复默认值。 */
#include "test.h"
#include "params.h"

void ut_params(void)
{
    param_store_t store;
    char buf[128];
    float v;
    int n;

    tf_begin("params/注册与校验");
    params_init(&store);
    TF_CHECK_INT(params_add(&store, "kp", 1.0f, 0.0f, 10.0f), 0);
    TF_CHECK_INT(params_add(&store, "ki", 0.5f, 0.0f, 5.0f), 0);
    TF_CHECK_INT(params_add(&store, "kp", 2.0f, 0.0f, 10.0f), -1);
    TF_CHECK_INT(params_add(&store, "bad", 5.0f, 0.0f, 1.0f), -1);
    TF_CHECK_INT(params_add(&store, NULL, 0.0f, 0.0f, 1.0f), -1);
    TF_CHECK_INT(params_add(&store,
                            "name_is_way_too_long_for_store", 0.0f, 0.0f, 1.0f), -1);
    TF_CHECK_INT(store.count, 2);
    tf_end();

    tf_begin("params/读写与越界拒绝");
    TF_CHECK_INT(params_set(&store, "kp", 5.5f), 0);
    TF_CHECK_INT(params_get(&store, "kp", &v), 0);
    TF_CHECK_NEAR(v, 5.5f, 1e-6);
    TF_CHECK_INT(params_set(&store, "kp", 11.0f), -1);
    TF_CHECK_INT(params_set(&store, "nope", 1.0f), -1);
    TF_CHECK_INT(params_get(&store, "nope", &v), -1);
    tf_end();

    tf_begin("params/序列化与反序列化");
    n = params_serialize(&store, buf, sizeof(buf));
    TF_CHECK(n > 0);
    TF_CHECK(strstr(buf, "kp=5.5") != NULL);

    TF_CHECK_INT(params_set(&store, "kp", 2.0f), 0);
    TF_CHECK_INT(params_deserialize(&store, buf), 2);
    TF_CHECK_INT(params_get(&store, "kp", &v), 0);
    TF_CHECK_NEAR(v, 5.5f, 1e-4);

    /* 小缓冲应报错 */
    TF_CHECK_INT(params_serialize(&store, buf, 4), -1);
    TF_CHECK_INT(params_deserialize(&store, ""), -1);
    TF_CHECK_INT(params_deserialize(&store, "unknown=1\n"), -1);
    tf_end();

    tf_begin("params/恢复默认值");
    TF_CHECK_INT(params_reset(&store, "kp"), 0);
    TF_CHECK_INT(params_get(&store, "kp", &v), 0);
    TF_CHECK_NEAR(v, 1.0f, 1e-6);
    TF_CHECK_INT(params_set(&store, "ki", 3.0f), 0);
    params_reset_all(&store);
    TF_CHECK_INT(params_get(&store, "ki", &v), 0);
    TF_CHECK_NEAR(v, 0.5f, 1e-6);
    tf_end();
}
