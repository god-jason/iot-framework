#ifndef ODOMETRY_H
#define ODOMETRY_H

/*
 * odometry.h - 差速底盘里程计
 *
 * 输入左右轮编码器累计脉冲，输出世界坐标位姿：
 *   - 采用圆弧精确积分（小角度时退化为中点法），比一阶近似更准；
 *   - 第一次 update 只建立基准，不产生位移；
 *   - 无动态内存，对象由调用者静态分配。
 */

#include <stdint.h>
#include "kinematics.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    diff_drive_t drive;     /* 底盘参数副本 */
    pose2d_t pose;          /* 当前位姿 */

    int32_t last_left;      /* 上次左轮脉冲 */
    int32_t last_right;     /* 上次右轮脉冲 */

    bool primed;            /* 是否已建立脉冲基准 */
} odometry_t;

/*
 * 初始化里程计。
 * 返回：0 成功；-1 参数非法（drive/out 为空或底盘参数非法）。
 */
int odometry_init(odometry_t *odom, const diff_drive_t *drive,
                  float x0, float y0, float theta0);

/* 重置位姿，并清除脉冲基准（下次 update 重新建立） */
int odometry_reset(odometry_t *odom, float x, float y, float theta);

/*
 * 用最新左右轮累计脉冲更新位姿。
 * 返回：0 成功；-1 参数非法。
 * 说明：pose 输出当前位姿，可为 NULL。
 */
int odometry_update(odometry_t *odom, int32_t left_ticks, int32_t right_ticks,
                    pose2d_t *pose);

/*
 * 用直接测量的左右轮位移（米）更新位姿，便于仿真或无编码器场景。
 * 返回：0 成功；-1 参数非法。
 */
int odometry_update_distance(odometry_t *odom, float left_m, float right_m,
                             pose2d_t *pose);

#ifdef __cplusplus
}
#endif

#endif /* ODOMETRY_H */
