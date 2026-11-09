/* localization 单元测试：EKF 预测、位姿观测与偏航观测。 */
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

    tf_begin("localization/初始化参数校验");
    TF_CHECK_INT(loc_ekf_init(NULL, 0, 0, 0, 0.01f, 0.01f, 0.1f, 0.1f), -1);
    TF_CHECK_INT(loc_ekf_init(&ekf, 0, 0, 0, 0.0f, 0.01f, 0.1f, 0.1f), -1);
    TF_CHECK_INT(loc_ekf_init(&ekf, 0, 0, 0, 0.01f, 0.01f, 0.1f, 0.1f), 0);
    tf_end();

    tf_begin("localization/直线预测");
    for (i = 0; i < 10; i++) {
        loc_ekf_predict(&ekf, 1.0f, 0.0f, 0.1f);
    }
    est = loc_ekf_pose(&ekf);
    TF_CHECK(est != NULL);
    TF_CHECK_NEAR(est->x, 1.0f, 1e-4);
    TF_CHECK_NEAR(est->y, 0.0f, 1e-4);
    TF_CHECK_NEAR(est->theta, 0.0f, 1e-4);
    tf_end();

    tf_begin("localization/位姿观测收敛");
    x_before = ekf.state.x;
    measurement.x = 0.9f;
    measurement.y = 0.0f;
    measurement.theta = 0.0f;
    TF_CHECK_INT(loc_ekf_update_pose(&ekf, &measurement), 0);
    TF_CHECK(ekf.state.x < x_before);
    TF_CHECK(ekf.state.x > 0.9f);
    tf_end();

    tf_begin("localization/偏航观测与角度回绕");
    loc_ekf_reset(&ekf, 0.0f, 0.0f, 0.0f);
    for (i = 0; i < 10; i++) {
        loc_ekf_predict(&ekf, 0.0f, 1.0f, 0.1f);
    }
    theta_before = ekf.state.theta;
    TF_CHECK_NEAR(theta_before, 1.0f, 1e-4);
    TF_CHECK_INT(loc_ekf_update_yaw(&ekf, 0.0f), 0);
    TF_CHECK(ekf.state.theta < theta_before);
    TF_CHECK(ekf.state.theta > 0.0f);

    loc_ekf_reset(&ekf, 0.0f, 0.0f, 3.0f);
    TF_CHECK_INT(loc_ekf_update_yaw(&ekf, -3.0f), 0);
    TF_CHECK(ekf.state.theta > 3.0f || ekf.state.theta < -3.0f);
    tf_end();
}
