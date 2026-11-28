/*
 * kinematics_test.c - 运动学测试（对应 kinematics.c）
 *
 * 覆盖：差速轮正逆解与单位换算、阿克曼直行/转向几何、
 *       两连杆机械臂正逆解与不可达判定。
 */

#include "test.h"
#include "kinematics.h"

void ut_kinematics(void)
{
    diff_drive_t diff;
    ackermann_t ack;
    float vl;
    float vr;
    float v;
    float w;
    float sl;
    float sr;
    float x;
    float y;
    float q1;
    float q2;

    tf_begin("kinematics/差速轮参数校验");

    TF_CHECK_INT(diff_drive_init(NULL, 0.05f, 0.2f, 1000.0f), -1);
    TF_CHECK_INT(diff_drive_init(&diff, 0.0f, 0.2f, 1000.0f), -1);
    TF_CHECK_INT(diff_drive_init(&diff, 0.05f, 0.0f, 1000.0f), -1);
    TF_CHECK_INT(diff_drive_init(&diff, 0.05f, 0.2f, 0.0f), -1);
    TF_CHECK_INT(diff_drive_init(&diff, 0.05f, 0.2f, 1000.0f), 0);

    tf_end();

    tf_begin("kinematics/差速轮正逆解");

    TF_CHECK_INT(diff_drive_unicycle(&diff, 0.5f, 0.2f, &vl, &vr), 0);
    TF_CHECK_NEAR(vl, 0.48f, 1e-5);
    TF_CHECK_NEAR(vr, 0.52f, 1e-5);

    TF_CHECK_INT(diff_drive_inverse(&diff, vl, vr, &v, &w), 0);
    TF_CHECK_NEAR(v, 0.5f, 1e-5);
    TF_CHECK_NEAR(w, 0.2f, 1e-5);

    /* 原地旋转 */
    TF_CHECK_INT(diff_drive_unicycle(&diff, 0.0f, 1.0f, &vl, &vr), 0);
    TF_CHECK(vl < 0.0f);
    TF_CHECK(vr > 0.0f);

    TF_CHECK_INT(diff_drive_unicycle(NULL, 0.0f, 0.0f, &vl, &vr), -1);
    TF_CHECK_INT(diff_drive_unicycle(&diff, 0.0f, 0.0f, NULL, &vr), -1);
    TF_CHECK_INT(diff_drive_inverse(&diff, 0.0f, 0.0f, NULL, &w), -1);

    tf_end();

    tf_begin("kinematics/脉冲与位移换算");

    TF_CHECK_NEAR(diff_drive_ticks_to_meters(&diff, 1000), 0.3141593f, 1e-4);
    TF_CHECK_INT(diff_drive_meters_to_ticks(&diff, 0.3141593f), 1000);
    TF_CHECK_NEAR(diff_drive_ticks_to_meters(&diff, -500), -0.1570796f, 1e-4);
    TF_CHECK_INT(diff_drive_meters_to_ticks(&diff, 0.0f), 0);
    TF_CHECK_NEAR(diff_drive_ticks_to_meters(NULL, 100), 0.0f, 1e-6);

    tf_end();

    tf_begin("kinematics/阿克曼");

    TF_CHECK_INT(ackermann_init(&ack, 0.6f, 0.4f), 0);
    TF_CHECK_INT(ackermann_init(&ack, 0.0f, 0.4f), -1);

    TF_CHECK_INT(ackermann_unicycle(&ack, 1.0f, 0.0f, &vl, &vr, &sl, &sr), 0);
    TF_CHECK_NEAR(vl, 1.0f, 1e-6);
    TF_CHECK_NEAR(vr, 1.0f, 1e-6);
    TF_CHECK_NEAR(sl, 0.0f, 1e-6);
    TF_CHECK_NEAR(sr, 0.0f, 1e-6);

    TF_CHECK_INT(ackermann_unicycle(&ack, 1.0f, 0.2f, &vl, &vr, &sl, &sr), 0);
    TF_CHECK(vl < vr);          /* 左转为内侧轮更慢 */
    TF_CHECK(sl > 0.2f);        /* 内侧转角更大 */
    TF_CHECK(sr < 0.2f);

    /* 转向半径小于半个轮距：几何不可实现 */
    TF_CHECK_INT(ackermann_unicycle(&ack, 1.0f, 1.5f, &vl, &vr, &sl, &sr), -1);
    TF_CHECK_INT(ackermann_unicycle(NULL, 1.0f, 0.0f, &vl, &vr, &sl, &sr), -1);

    tf_end();

    tf_begin("kinematics/两连杆机械臂");

    TF_CHECK_INT(arm2_fk(1.0f, 1.0f, 0.0f, 0.0f, &x, &y), 0);
    TF_CHECK_NEAR(x, 2.0f, 1e-5);
    TF_CHECK_NEAR(y, 0.0f, 1e-5);

    TF_CHECK_INT(arm2_fk(1.0f, 1.0f, 0.5f, 0.3f, &x, &y), 0);
    TF_CHECK_INT(arm2_ik(1.0f, 1.0f, x, y, true, &q1, &q2), 0);
    TF_CHECK_NEAR(q1, 0.5f, 1e-4);
    TF_CHECK_NEAR(q2, 0.3f, 1e-4);

    TF_CHECK_INT(arm2_ik(1.0f, 1.0f, x, y, false, &q1, &q2), 0);
    TF_CHECK(q2 < 0.0f);

    /* 超出工作空间（连杆总长 2） */
    TF_CHECK_INT(arm2_ik(1.0f, 1.0f, 3.0f, 0.0f, true, &q1, &q2), -1);

    TF_CHECK_INT(arm2_fk(0.0f, 1.0f, 0.0f, 0.0f, &x, &y), -1);
    TF_CHECK_INT(arm2_fk(1.0f, 1.0f, 0.0f, 0.0f, NULL, &y), -1);
    TF_CHECK_INT(arm2_ik(1.0f, 1.0f, 0.0f, 0.0f, true, NULL, &q2), -1);

    tf_end();
}
