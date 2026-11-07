/* algorithm/interp 单元测试：线性、查表（含端点夹取）与 Catmull-Rom 三次插值。 */
#include "test.h"
#include "interpolation.h"

void ut_interp(void)
{
    static const float xs[] = { 0.0f, 10.0f, 20.0f };
    static const float ys[] = { 0.0f, 100.0f, 200.0f };
    static const float pts4[] = { 0.0f, 1.0f, 2.0f, 3.0f };
    static const float pts5[] = { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };
    float out;

    tf_begin("algorithm/interp 线性插值");
    TF_CHECK_NEAR(interpolation_linear(5.0f, 0.0f, 0.0f, 10.0f, 100.0f), 50.0f, 1e-4);
    TF_CHECK_NEAR(interpolation_linear(0.0f, 0.0f, 0.0f, 10.0f, 100.0f), 0.0f, 1e-4);
    TF_CHECK_NEAR(interpolation_linear(10.0f, 0.0f, 0.0f, 10.0f, 100.0f), 100.0f, 1e-4);
    TF_CHECK_NEAR(interpolation_linear(-5.0f, 0.0f, 0.0f, 10.0f, 100.0f), -50.0f, 1e-4);
    /* x0 == x1 时退化为返回 y0 */
    TF_CHECK_NEAR(interpolation_linear(7.0f, 5.0f, 1.0f, 5.0f, 9.0f), 1.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/interp 查表");
    out = 0.0f;
    TF_CHECK_INT(interpolation_lookup(5.0f, xs, ys, 3, &out), 0);
    TF_CHECK_NEAR(out, 50.0f, 1e-4);
    TF_CHECK_INT(interpolation_lookup(10.0f, xs, ys, 3, &out), 0);
    TF_CHECK_NEAR(out, 100.0f, 1e-4);
    TF_CHECK_INT(interpolation_lookup(0.0f, xs, ys, 3, &out), 0);
    TF_CHECK_NEAR(out, 0.0f, 1e-4);
    TF_CHECK_INT(interpolation_lookup(20.0f, xs, ys, 3, &out), 0);
    TF_CHECK_NEAR(out, 200.0f, 1e-4);
    /* 低于/高于表范围时夹取端点 */
    TF_CHECK_INT(interpolation_lookup(-5.0f, xs, ys, 3, &out), 0);
    TF_CHECK_NEAR(out, 0.0f, 1e-4);
    TF_CHECK_INT(interpolation_lookup(25.0f, xs, ys, 3, &out), 0);
    TF_CHECK_NEAR(out, 200.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/interp 查表边界与错误");
    TF_CHECK_INT(interpolation_lookup(5.0f, xs, ys, 1, &out), -1);
    TF_CHECK_INT(interpolation_lookup(5.0f, NULL, ys, 3, &out), -1);
    TF_CHECK_INT(interpolation_lookup(5.0f, xs, NULL, 3, &out), -1);
    TF_CHECK_INT(interpolation_lookup(5.0f, xs, ys, 3, NULL), -1);
    tf_end();

    tf_begin("algorithm/interp Catmull-Rom 节点与中点");
    TF_CHECK_NEAR(interpolation_cubic(0.0f, pts4, 4), 0.0f, 1e-4);
    TF_CHECK_NEAR(interpolation_cubic(1.0f, pts4, 4), 1.0f, 1e-4);
    TF_CHECK_NEAR(interpolation_cubic(2.0f, pts4, 4), 2.0f, 1e-4);
    TF_CHECK_NEAR(interpolation_cubic(3.0f, pts4, 4), 3.0f, 1e-4);
    TF_CHECK_NEAR(interpolation_cubic(0.5f, pts4, 4), 0.4375f, 1e-4);
    TF_CHECK_NEAR(interpolation_cubic(1.5f, pts4, 4), 1.5f, 1e-4);
    tf_end();

    tf_begin("algorithm/interp Catmull-Rom 端点夹取");
    TF_CHECK_NEAR(interpolation_cubic(-2.0f, pts4, 4), 0.0f, 1e-4);
    TF_CHECK_NEAR(interpolation_cubic(10.0f, pts4, 4), 3.0f, 1e-4);
    TF_CHECK_NEAR(interpolation_cubic(0.0f, pts5, 5), 1.0f, 1e-4);
    TF_CHECK_NEAR(interpolation_cubic(2.0f, pts5, 5), 4.0f, 1e-4);
    TF_CHECK_NEAR(interpolation_cubic(3.0f, pts5, 5), 8.0f, 1e-4);
    TF_CHECK_NEAR(interpolation_cubic(4.0f, pts5, 5), 16.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/interp Catmull-Rom 错误参数");
    TF_CHECK_NEAR(interpolation_cubic(1.0f, NULL, 4), 0.0f, 1e-4);
    TF_CHECK_NEAR(interpolation_cubic(1.0f, pts4, 3), 0.0f, 1e-4);
    tf_end();
}
