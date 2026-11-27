/*
 * cfg_test.c - cfg.h（配置结构体 <-> var_t / JSON 映射宏）单元测试
 *
 * 覆盖：string 数组容量、默认值填充、struct->var、var->struct（缺失字段）、
 * JSON 往返、JSON 解析失败与字符串截断、全部数值类型往返。
 */

#include "test.h"
#include "cfg.h"

/* ---------------- 测试用配置表 ---------------- */

#define T_DEV_CFG(X) \
    X(int32,  id, 0) \
    X(bool,   enabled, true) \
    X(float32, temperature, 25.0f) \
    X(string, name, "device") \
    X(string, sn, "SN0001", 32)

CFG_DEFINE(t_dev, T_DEV_CFG)

#define T_ALL_CFG(X) \
    X(bool, b, false) \
    X(uint8, u8, 1) \
    X(int8, i8, -1) \
    X(uint16, u16, 2) \
    X(int16, i16, -2) \
    X(uint32, u32, 3) \
    X(int32, i32, -3) \
    X(uint64, u64, 4) \
    X(int64, i64, -4) \
    X(float32, f32, 1.5f) \
    X(float64, f64, 2.5) \
    X(string, s, "x", 8)

CFG_DEFINE(t_all, T_ALL_CFG)

/* ---------------- 辅助 ---------------- */

static void dev_fill(t_dev *d, int32_t id, bool enabled, float temp,
                     const char *name, const char *sn)
{
    memset(d, 0, sizeof(*d));
    d->id = id;
    d->enabled = enabled;
    d->temperature = temp;
    str_copy(d->name, sizeof(d->name), name);
    str_copy(d->sn, sizeof(d->sn), sn);
}

/* ---------------- 测试 ---------------- */

void ut_cfg(void)
{
    t_dev d;
    var_t v;

    tf_begin("cfg/string 数组容量");
    {
        TF_CHECK_INT((int)sizeof(((t_dev *)0)->name),
                     (int)sizeof("device"));    /* 省略容量取默认值长度 */
        TF_CHECK_INT((int)sizeof(((t_dev *)0)->sn), 32);  /* 显式容量 */
    }
    tf_end();

    tf_begin("cfg/默认值填充");
    {
        var_t none = { VAR_NONE };

        memset(&d, 0, sizeof(d));
        CFG_FROM_VAR(t_dev, &d, &none);
        TF_CHECK_INT(d.id, 0);
        TF_CHECK(d.enabled == true);
        TF_CHECK_NEAR(d.temperature, 25.0f, 1e-6);
        TF_CHECK_STR(d.name, "device");
        TF_CHECK_STR(d.sn, "SN0001");
    }
    tf_end();

    tf_begin("cfg/struct -> var");
    {
        int32_t id = 0;
        bool enabled = true;
        float temp = 0.0f;

        dev_fill(&d, 7, false, 36.5f, "sensor", "SN123");
        v.type = VAR_NONE;
        CFG_TO_VAR(t_dev, &d, &v);
        TF_CHECK_INT(v.type, VAR_OBJECT);
        TF_CHECK(var_object_get_int32(&v, "id", &id) && id == 7);
        TF_CHECK(var_object_get_bool(&v, "enabled", &enabled) && !enabled);
        TF_CHECK(var_object_get_float32(&v, "temperature", &temp));
        TF_CHECK_NEAR(temp, 36.5f, 1e-6);
        TF_CHECK_STR(var_object_get_str(&v, "name"), "sensor");
        TF_CHECK_STR(var_object_get_str(&v, "sn"), "SN123");
        var_free(&v);
    }
    tf_end();

    tf_begin("cfg/var -> struct 缺失字段");
    {
        v.type = VAR_NONE;
        TF_CHECK(var_object_new(&v) != NULL);
        TF_CHECK(var_object_put_int32(&v, "id", 42));
        TF_CHECK(var_object_put_str(&v, "name", "abc"));

        dev_fill(&d, 0, false, 0.0f, "", "");
        CFG_FROM_VAR(t_dev, &d, &v);
        TF_CHECK_INT(d.id, 42);
        TF_CHECK_STR(d.name, "abc");
        /* 未出现的字段回落到默认值 */
        TF_CHECK(d.enabled == true);
        TF_CHECK_NEAR(d.temperature, 25.0f, 1e-6);
        TF_CHECK_STR(d.sn, "SN0001");
        var_free(&v);
    }
    tf_end();

    tf_begin("cfg/JSON 往返");
    {
        char buf[128];
        char *json;

        dev_fill(&d, 9, false, 12.25f, "arm01", "SN999");
        json = CFG_TO_JSON(t_dev, &d);
        TF_CHECK(json != NULL);
        if (json != NULL) {
            t_dev in;

            memset(&in, 0, sizeof(in));
            TF_CHECK(CFG_FROM_JSON(t_dev, &in, json, buf, sizeof(buf)));
            TF_CHECK_INT(in.id, 9);
            TF_CHECK(in.enabled == false);
            TF_CHECK_NEAR(in.temperature, 12.25f, 1e-6);
            TF_CHECK_STR(in.name, "arm01");
            TF_CHECK_STR(in.sn, "SN999");
            var_free_json(json);
        }
    }
    tf_end();

    tf_begin("cfg/JSON 解析失败回默认");
    {
        char buf[128];

        dev_fill(&d, 1, false, 1.0f, "x", "y");
        TF_CHECK(!CFG_FROM_JSON(t_dev, &d, "{bad json", buf, sizeof(buf)));
        TF_CHECK_INT(d.id, 0);
        TF_CHECK(d.enabled == true);
        TF_CHECK_NEAR(d.temperature, 25.0f, 1e-6);
        TF_CHECK_STR(d.name, "device");
        TF_CHECK_STR(d.sn, "SN0001");
    }
    tf_end();

    tf_begin("cfg/JSON 字符串截断");
    {
        char buf[128];

        memset(&d, 0, sizeof(d));
        TF_CHECK(CFG_FROM_JSON(t_dev, &d,
                               "{\"name\":\"abcdefghijklm\"}",
                               buf, sizeof(buf)));
        TF_CHECK_STR(d.name, "abcdef");       /* char name[7] -> 最多 6 字符 */
        TF_CHECK_STR(d.sn, "SN0001");
    }
    tf_end();

    tf_begin("cfg/全类型往返");
    {
        t_all a;
        t_all b;
        var_t av = { VAR_NONE };

        memset(&a, 0, sizeof(a));
        a.b = true;
        a.u8 = 200;
        a.i8 = -100;
        a.u16 = 60000;
        a.i16 = -30000;
        a.u32 = 4000000000U;
        a.i32 = -2000000000;
        a.u64 = 1234567890123ULL;
        a.i64 = -1234567890123LL;
        a.f32 = -1.25f;
        a.f64 = 3.5;
        str_copy(a.s, sizeof(a.s), "ok");

        CFG_TO_VAR(t_all, &a, &av);
        TF_CHECK_INT(av.type, VAR_OBJECT);

        memset(&b, 0, sizeof(b));
        CFG_FROM_VAR(t_all, &b, &av);
        TF_CHECK(b.b == a.b);
        TF_CHECK(b.u8 == a.u8);
        TF_CHECK(b.i8 == a.i8);
        TF_CHECK(b.u16 == a.u16);
        TF_CHECK(b.i16 == a.i16);
        TF_CHECK(b.u32 == a.u32);
        TF_CHECK(b.i32 == a.i32);
        TF_CHECK(b.u64 == a.u64);
        TF_CHECK(b.i64 == a.i64);
        TF_CHECK_NEAR(b.f32, a.f32, 1e-6);
        TF_CHECK_NEAR(b.f64, a.f64, 1e-12);
        TF_CHECK_STR(b.s, "ok");
        var_free(&av);
    }
    tf_end();
}
