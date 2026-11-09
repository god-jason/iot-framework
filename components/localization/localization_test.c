/*
 * localization_test.c - EKF 定位测试（对应 localization.c）
 *
 * 覆盖：初始化校验、直线/转向预测、绝对位姿观测收敛、
 *       偏航观测与角度回绕、重置、非法参数。
 */

#include "test.h"
#include "localization.h"

void ut_localization(void)
{
    localization_ekf_t ekf;
    pose2d_t measurement;
    const pose2d_t *est;
    float x_before;
    float theta_before;
    int i;

    tf_begin("localization/初始化");

    TF_CHECK_INT(loc_ekf_init(NULL, 0, 0, 0, 0.01f, 0.01f, 0.1f, 0.1f), -1);
    TF_CHECK_INT(loc_ekf_init(&ekf, 0, 0, 0, 0.0f, 0.01f, 0.1f, 0.1f), -1);
    TF_CHECK_INT(loc_ekf_init(&ekf, 0, 0, 0, 0.01f, 0.01f, 0.0f, 0.1f), -1);
    TF_CHECK_INT(loc_ekf_init(&ekf, 1.0f, 2.0f, 0.5f, 0.01f, 0.01f,
                              0.1f, 0.1f), 0);
    est = loc_ekf_pose(&ekf);
    TF_CHECK(est != NULL);
    TF_CHECK_NEAR(est->x, 1.0f, 1e-6);
    TF_CHECK_NEAR(est->y, 2.0f, 1e-6);
    TF_CHECK_NEAR(est->theta, 0.5f, 1e-6);
    TF_CHECK(loc_ekf_pose(NULL) == NULL);

    tf_end();

    tf_begin("localization/预测");

    loc_ekf_reset(&ekf, 0.0f, 0.0f, 0.0f);
    for (i = 0; i < 10; i++) {
        loc_ekf_predict(&ekf, 1.0f, 0.0f, 0.1f);
    }
    est = loc_ekf_pose(&ekf);
    TF_CHECK_NEAR(est->x, 1.0f, 1e-4);
    TF_CHECK_NEAR(est->y, 0.0f, 1e-4);
    TF_CHECK_NEAR(est->theta, 0.0f, 1e-4);

    /* 原地转向 */
    loc_ekf_reset(&ekf, 0.0f, 0.0f, 0.0f);
    for (i = 0; i < 10; i++) {
        loc_ekf_predict(&ekf, 0.0f, 1.0f, 0.1f);
    }
    TF_CHECK_NEAR(ekf.state.theta, 1.0f, 1e-4);

    /* 角度归一化：持续旋转应回到 [-pi, pi] */
    for (i = 0; i < 100; i++) {
        loc_ekf_predict(&ekf, 0.0f, 1.0f, 0.1f);
    }
    TF_CHECK(ekf.state.theta >= -3.1416f && ekf.state.theta <= 3.1416f);

    TF_CHECK_INT(loc_ekf_predict(NULL, 0.0f, 0.0f, 0.1f), -1);
    TF_CHECK_INT(loc_ekf_predict(&ekf, 0.0f, 0.0f, 0.0f), -1);

    tf_end();

    tf_begin("localization/位姿观测");

    loc_ekf_reset(&ekf, 0.0f, 0.0f, 0.0f);
    for (i = 0; i < 10; i++) {
        loc_ekf_predict(&ekf, 1.0f, 0.0f, 0.1f);
    }
    x_before = ekf.state.x;
    measurement.x = 0.9f;
    measurement.y = 0.0f;
    measurement.theta = 0.0f;
    TF_CHECK_INT(loc_ekf_update_pose(&ekf, &measurement), 0);
    TF_CHECK(ekf.state.x < x_before);
    TF_CHECK(ekf.state.x > 0.9f);

    /* 协方差应随观测下降 */
    TF_CHECK(ekf.P[0][0] < 1.0f);

    TF_CHECK_INT(loc_ekf_update_pose(&ekf, NULL), -1);
    TF_CHECK_INT(loc_ekf_update_pose(NULL, &measurement), -1);

    tf_end();

    tf_begin("localization/偏航观测与重置");

    loc_ekf_reset(&ekf, 0.0f, 0.0f, 0.0f);
    for (i = 0; i < 10; i++) {
        loc_ekf_predict(&ekf, 0.0f, 1.0f, 0.1f);
    }
    theta_before = ekf.state.theta;
    TF_CHECK_NEAR(theta_before, 1.0f, 1e-4);
    TF_CHECK_INT(loc_ekf_update_yaw(&ekf, 0.0f), 0);
    TF_CHECK(ekf.state.theta < theta_before);
    TF_CHECK(ekf.state.theta > 0.0f);

    TF_CHECK_INT(loc_ekf_update_yaw(NULL, 0.0f), -1);

    TF_CHECK_INT(loc_ekf_reset(&ekf, 3.0f, 4.0f, 0.0f), 0);
    TF_CHECK_NEAR(ekf.state.x, 3.0f, 1e-6);
    TF_CHECK_NEAR(ekf.state.y, 4.0f, 1e-6);
    TF_CHECK_INT(loc_ekf_reset(NULL, 0.0f, 0.0f, 0.0f), -1);

    tf_end();
}
