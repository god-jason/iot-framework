/*
 * perception_test.c - 扫描抽象测试（对应 perception.c）
 *
 * 覆盖：环形缓冲覆盖、按时间顺序读取、坐标转换、角度窗口最近点。
 */

#include "test.h"
#include "perception.h"

void ut_perception(void)
{
    scan_point_t buf[4];
    scan_buffer_t scan;
    scan_point_t points[5];
    scan_point_t p;
    pose2d_t pose;
    float xs[2];
    float ys[2];
    float x;
    float y;
    float range;
    int i;

    tf_begin("perception/初始化与环形覆盖");

    TF_CHECK_INT(scan_init(NULL, buf, 4U), -1);
    TF_CHECK_INT(scan_init(&scan, NULL, 4U), -1);
    TF_CHECK_INT(scan_init(&scan, buf, 0U), -1);
    TF_CHECK_INT(scan_init(&scan, buf, 4U), 0);
    TF_CHECK_INT(scan_count(&scan), 0);

    for (i = 0; i < 5; i++) {
        points[i].angle = (float)i * 0.1f;
        points[i].range = (float)(i + 1);
    }
    TF_CHECK_INT(scan_push(&scan, points, 5U), 5);
    TF_CHECK_INT(scan_count(&scan), 4);      /* 容量 4，覆盖最旧点 */

    /* 最旧的是 points[1] */
    TF_CHECK_INT(scan_get(&scan, 0U, &p), 0);
    TF_CHECK_NEAR(p.range, 2.0f, 1e-6);
    TF_CHECK_INT(scan_get(&scan, 3U, &p), 0);
    TF_CHECK_NEAR(p.range, 5.0f, 1e-6);
    TF_CHECK_INT(scan_get(&scan, 4U, &p), -1);
    TF_CHECK_INT(scan_get(&scan, 0U, NULL), -1);

    scan_clear(&scan);
    TF_CHECK_INT(scan_count(&scan), 0);

    tf_end();

    tf_begin("perception/坐标转换");

    p.angle = 0.0f;
    p.range = 1.0f;
    TF_CHECK_INT(scan_to_cartesian(&p, &x, &y), 0);
    TF_CHECK_NEAR(x, 1.0f, 1e-6);
    TF_CHECK_NEAR(y, 0.0f, 1e-6);

    p.angle = 1.5707963f;
    TF_CHECK_INT(scan_to_cartesian(&p, &x, &y), 0);
    TF_CHECK_NEAR(x, 0.0f, 1e-5);
    TF_CHECK_NEAR(y, 1.0f, 1e-5);

    pose.x = 1.0f;
    pose.y = 2.0f;
    pose.theta = 1.5707963f;
    p.angle = 0.0f;
    p.range = 1.0f;
    TF_CHECK_INT(scan_transform_world(&pose, &p, 1U, xs, ys), 0);
    TF_CHECK_NEAR(xs[0], 1.0f, 1e-5);
    TF_CHECK_NEAR(ys[0], 3.0f, 1e-5);

    TF_CHECK_INT(scan_to_cartesian(NULL, &x, &y), -1);
    TF_CHECK_INT(scan_transform_world(NULL, &p, 1U, xs, ys), -1);

    tf_end();

    tf_begin("perception/角度窗口最近点");

    points[0].angle = -0.5f; points[0].range = 3.0f;
    points[1].angle = 0.0f;  points[1].range = 1.0f;
    points[2].angle = 0.5f;  points[2].range = 2.0f;

    TF_CHECK_INT(scan_nearest(points, 3U, -0.2f, 0.6f, &range), 0);
    TF_CHECK_NEAR(range, 1.0f, 1e-6);

    /* 窗口内无点 */
    TF_CHECK_INT(scan_nearest(points, 3U, 1.0f, 2.0f, &range), -1);

    /* 无效距离被忽略 */
    points[1].range = 0.0f;
    TF_CHECK_INT(scan_nearest(points, 3U, -0.2f, 0.6f, &range), 0);
    TF_CHECK_NEAR(range, 2.0f, 1e-6);

    TF_CHECK_INT(scan_nearest(NULL, 3U, 0.0f, 1.0f, &range), -1);
    TF_CHECK_INT(scan_nearest(points, 0U, 0.0f, 1.0f, &range), -1);
    TF_CHECK_INT(scan_nearest(points, 3U, 0.0f, 1.0f, NULL), -1);

    tf_end();
}
