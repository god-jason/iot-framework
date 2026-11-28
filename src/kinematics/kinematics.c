/*
 * kinematics.c - 机器人运动学实现
 *
 * 实现要点：
 *   - 差速轮的分速公式：v_l = v - w*L/2，v_r = v + w*L/2；
 *   - 阿克曼按瞬时转向中心计算内外轮速度与转角，转角接近 0 时按直行处理；
 *   - 两连杆逆解使用余弦定理，不可达时返回 -1。
 */

#include "kinematics.h"

#include <stddef.h>

#include <math.h>

/* 判断浮点参数是否可用于除法/开方 */
static bool valid_positive(float v)
{
    return (v > 0.0f) && isfinite(v);
}

/* ============================== 差速轮 ============================== */

int diff_drive_init(diff_drive_t *drive, float wheel_radius,
                    float wheel_base, float ticks_per_rev)
{
    if (drive == NULL || !valid_positive(wheel_radius) ||
        !valid_positive(wheel_base) || !valid_positive(ticks_per_rev)) {
        return -1;
    }
    drive->wheel_radius = wheel_radius;
    drive->wheel_base = wheel_base;
    drive->ticks_per_rev = ticks_per_rev;
    return 0;
}

int diff_drive_unicycle(const diff_drive_t *drive, float v, float w,
                        float *v_left, float *v_right)
{
    float half_base;

    if (drive == NULL || v_left == NULL || v_right == NULL) {
        return -1;
    }
    if (!valid_positive(drive->wheel_base)) {
        return -1;
    }

    half_base = drive->wheel_base * 0.5f;
    *v_left = v - w * half_base;
    *v_right = v + w * half_base;
    return 0;
}

int diff_drive_inverse(const diff_drive_t *drive, float v_left, float v_right,
                       float *v, float *w)
{
    if (drive == NULL || v == NULL || w == NULL) {
        return -1;
    }
    if (!valid_positive(drive->wheel_base)) {
        return -1;
    }

    *v = (v_left + v_right) * 0.5f;
    *w = (v_right - v_left) / drive->wheel_base;
    return 0;
}

float diff_drive_ticks_to_meters(const diff_drive_t *drive, int32_t ticks)
{
    float circumference;

    if (drive == NULL || !valid_positive(drive->wheel_radius) ||
        !valid_positive(drive->ticks_per_rev)) {
        return 0.0f;
    }
    circumference = 2.0f * 3.14159265358979323846f * drive->wheel_radius;
    return ((float)ticks / drive->ticks_per_rev) * circumference;
}

int32_t diff_drive_meters_to_ticks(const diff_drive_t *drive, float meters)
{
    float circumference;
    float ticks;

    if (drive == NULL || !valid_positive(drive->wheel_radius) ||
        !valid_positive(drive->ticks_per_rev)) {
        return 0;
    }
    circumference = 2.0f * 3.14159265358979323846f * drive->wheel_radius;
    ticks = (meters / circumference) * drive->ticks_per_rev;
    return (int32_t)((ticks >= 0.0f) ? (ticks + 0.5f) : (ticks - 0.5f));
}

/* ============================== 阿克曼 ============================== */

int ackermann_init(ackermann_t *ack, float wheelbase, float track)
{
    if (ack == NULL || !valid_positive(wheelbase) || !valid_positive(track)) {
        return -1;
    }
    ack->wheelbase = wheelbase;
    ack->track = track;
    return 0;
}

int ackermann_unicycle(const ackermann_t *ack, float v, float steer,
                       float *v_rear_left, float *v_rear_right,
                       float *steer_left, float *steer_right)
{
    float tan_steer;
    float radius;      /* 后轴中心到瞬时转向中心的带符号半径 */

    if (ack == NULL || v_rear_left == NULL || v_rear_right == NULL ||
        steer_left == NULL || steer_right == NULL) {
        return -1;
    }
    if (!valid_positive(ack->wheelbase) || !valid_positive(ack->track)) {
        return -1;
    }

    tan_steer = tanf(steer);
    if (fabsf(tan_steer) < 1e-6f) {
        /* 直行：内外轮同速、同角 */
        *v_rear_left = v;
        *v_rear_right = v;
        *steer_left = 0.0f;
        *steer_right = 0.0f;
        return 0;
    }

    radius = ack->wheelbase / tan_steer;

    /* 内侧转向半径必须大于半个轮距，否则几何不可实现 */
    if (fabsf(radius) <= ack->track * 0.5f) {
        return -1;
    }

    *v_rear_left = v * (radius - ack->track * 0.5f) / radius;
    *v_rear_right = v * (radius + ack->track * 0.5f) / radius;
    *steer_left = atanf(ack->wheelbase / (radius - ack->track * 0.5f));
    *steer_right = atanf(ack->wheelbase / (radius + ack->track * 0.5f));
    return 0;
}

/* ============================== 两连杆机械臂 ============================== */

int arm2_fk(float l1, float l2, float q1, float q2, float *x, float *y)
{
    float c1;
    float s1;
    float c12;
    float s12;

    if (x == NULL || y == NULL) {
        return -1;
    }
    if (!valid_positive(l1) || !valid_positive(l2)) {
        return -1;
    }

    c1 = cosf(q1);
    s1 = sinf(q1);
    c12 = cosf(q1 + q2);
    s12 = sinf(q1 + q2);

    *x = l1 * c1 + l2 * c12;
    *y = l1 * s1 + l2 * s12;
    return 0;
}

int arm2_ik(float l1, float l2, float x, float y, bool elbow_up,
            float *q1, float *q2)
{
    float r2;
    float cos_q2;
    float q2_sol;

    if (q1 == NULL || q2 == NULL) {
        return -1;
    }
    if (!valid_positive(l1) || !valid_positive(l2)) {
        return -1;
    }

    r2 = x * x + y * y;
    cos_q2 = (r2 - l1 * l1 - l2 * l2) / (2.0f * l1 * l2);
    if (cos_q2 > 1.0f + 1e-6f || cos_q2 < -1.0f - 1e-6f) {
        return -1;              /* 目标超出工作空间 */
    }
    if (cos_q2 > 1.0f) {
        cos_q2 = 1.0f;
    } else if (cos_q2 < -1.0f) {
        cos_q2 = -1.0f;
    }

    q2_sol = acosf(cos_q2);
    if (!elbow_up) {
        q2_sol = -q2_sol;
    }

    *q2 = q2_sol;
    *q1 = atan2f(y, x) - atan2f(l2 * sinf(q2_sol),
                                l1 + l2 * cosf(q2_sol));
    return 0;
}
