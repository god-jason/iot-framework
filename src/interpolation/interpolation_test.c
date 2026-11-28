/*
 * interpolation_test.c - 插值测试（对应 interpolation.c）
 *
 * 覆盖：两点线性、分段查表（含端点钳位）、Catmull-Rom 三次曲线。
 */

#include "test.h"
#include "interpolation.h"

void ut_interp(void)
{
    tf_begin("interpolation/线性与查表");

    TF_CHECK_NEAR(interpolation_linear(0.5f, 0.0f, 0.0f, 1.0f, 10.0f), 5.0f,
                  1e-6);
    TF_CHECK_NEAR(interpolation_linear(2.0f, 0.0f, 0.0f, 1.0f, 10.0f), 20.0f,
                  1e-6);
    /* 退化：两点重合时返回 y0 */
    TF_CHECK_NEAR(interpolation_linear(3.0f, 1.0f, 7.0f, 1.0f, 9.0f), 7.0f,
                  1e-6);

    {
        static const float xs[3] = { 0.0f, 1.0f, 2.0f };
        static const float ys[3] = { 0.0f, 10.0f, 20.0f };
        float out = 0.0f;

        TF_CHECK_INT(interpolation_lookup(0.5f, xs, ys, 3U, &out), 0);
        TF_CHECK_NEAR(out, 5.0f, 1e-5);

        TF_CHECK_INT(interpolation_lookup(1.5f, xs, ys, 3U, &out), 0);
        TF_CHECK_NEAR(out, 15.0f, 1e-5);

        /* 区间外端点钳位，不外推 */
        TF_CHECK_INT(interpolation_lookup(-5.0f, xs, ys, 3U, &out), 0);
        TF_CHECK_NEAR(out, 0.0f, 1e-5);
        TF_CHECK_INT(interpolation_lookup(99.0f, xs, ys, 3U, &out), 0);
        TF_CHECK_NEAR(out, 20.0f, 1e-5);

        TF_CHECK_INT(interpolation_lookup(0.5f, xs, ys, 1U, &out), -1);
        TF_CHECK_INT(interpolation_lookup(0.5f, NULL, ys, 3U, &out), -1);
        TF_CHECK_INT(interpolation_lookup(0.5f, xs, NULL, 3U, &out), -1);
        TF_CHECK_INT(interpolation_lookup(0.5f, xs, ys, 3U, NULL), -1);
    }

    tf_end();

    tf_begin("interpolation/三次曲线");

    {
        static const float pts[4] = { 0.0f, 1.0f, 4.0f, 9.0f };

        /* 曲线必须经过采样点 */
        TF_CHECK_NEAR(interpolation_cubic(0.0f, pts, 4U), 0.0f, 1e-5);
        TF_CHECK_NEAR(interpolation_cubic(1.0f, pts, 4U), 1.0f, 1e-5);
        TF_CHECK_NEAR(interpolation_cubic(2.0f, pts, 4U), 4.0f, 1e-5);
        TF_CHECK_NEAR(interpolation_cubic(3.0f, pts, 4U), 9.0f, 1e-5);

        /* 端点外钳位 */
        TF_CHECK_NEAR(interpolation_cubic(-1.0f, pts, 4U), 0.0f, 1e-5);
        TF_CHECK_NEAR(interpolation_cubic(10.0f, pts, 4U), 9.0f, 1e-5);

        /* 参数非法返回 0 */
        TF_CHECK_NEAR(interpolation_cubic(1.0f, NULL, 4U), 0.0f, 1e-6);
        TF_CHECK_NEAR(interpolation_cubic(1.0f, pts, 3U), 0.0f, 1e-6);
    }

    tf_end();
}
