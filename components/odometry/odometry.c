/*
 * odometry.c - 差速底盘里程计实现
 *
 * 位姿更新采用圆弧模型：
 *   ds   = (dl + dr) / 2
 *   dth  = (dr - dl) / wheel_base
 *   R    = ds / dth
 *   dx   = R * (sin(th + dth) - sin(th))
 *   dy   = -R * (cos(th + dth) - cos(th))
 * 当 dth 接近 0 时退化为中点法，避免除零。
 */

#include "odometry.h"

#include <stddef.h>

#include <math.h>

#define ODOM_PI  3.14159265358979323846f

/* 把角度归一化到 [-pi, pi] */
static float wrap_pi(float angle)
{
    while (angle > ODOM_PI) {
        angle -= 2.0f * ODOM_PI;
    }
    while (angle < -ODOM_PI) {
        angle += 2.0f * ODOM_PI;
    }
    return angle;
}

int odometry_init(odometry_t *odom, const diff_drive_t *drive,
                  float x0, float y0, float theta0)
{
    if (odom == NULL || drive == NULL) {
        return -1;
    }
    if (drive->wheel_radius <= 0.0f || drive->wheel_base <= 0.0f ||
        drive->ticks_per_rev <= 0.0f) {
        return -1;
    }

    odom->drive = *drive;
    odom->pose.x = x0;
    odom->pose.y = y0;
    odom->pose.theta = wrap_pi(theta0);
    odom->last_left = 0;
    odom->last_right = 0;
    odom->primed = false;
    return 0;
}

int odometry_reset(odometry_t *odom, float x, float y, float theta)
{
    if (odom == NULL) {
        return -1;
    }
    odom->pose.x = x;
    odom->pose.y = y;
    odom->pose.theta = wrap_pi(theta);
    odom->primed = false;
    return 0;
}

int odometry_update_distance(odometry_t *odom, float left_m, float right_m,
                             pose2d_t *pose)
{
    float ds;
    float dth;
    float theta;
    float radius;

    if (odom == NULL) {
        return -1;
    }
    if (odom->drive.wheel_base <= 0.0f) {
        return -1;
    }

    ds = (left_m + right_m) * 0.5f;
    dth = (right_m - left_m) / odom->drive.wheel_base;

    theta = odom->pose.theta;
    if (fabsf(dth) < 1e-6f) {
        odom->pose.x += ds * cosf(theta + dth * 0.5f);
        odom->pose.y += ds * sinf(theta + dth * 0.5f);
    } else {
        radius = ds / dth;
        odom->pose.x += radius * (sinf(theta + dth) - sinf(theta));
        odom->pose.y -= radius * (cosf(theta + dth) - cosf(theta));
    }
    odom->pose.theta = wrap_pi(theta + dth);

    if (pose != NULL) {
        *pose = odom->pose;
    }
    return 0;
}

int odometry_update(odometry_t *odom, int32_t left_ticks, int32_t right_ticks,
                    pose2d_t *pose)
{
    int32_t dl_ticks;
    int32_t dr_ticks;

    if (odom == NULL) {
        return -1;
    }

    if (!odom->primed) {
        odom->last_left = left_ticks;
        odom->last_right = right_ticks;
        odom->primed = true;
        if (pose != NULL) {
            *pose = odom->pose;
        }
        return 0;
    }

    dl_ticks = left_ticks - odom->last_left;
    dr_ticks = right_ticks - odom->last_right;
    odom->last_left = left_ticks;
    odom->last_right = right_ticks;

    return odometry_update_distance(
        odom,
        diff_drive_ticks_to_meters(&odom->drive, dl_ticks),
        diff_drive_ticks_to_meters(&odom->drive, dr_ticks),
        pose);
}
