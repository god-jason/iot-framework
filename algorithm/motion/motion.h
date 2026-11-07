#ifndef MOTION_H
#define MOTION_H

/*
 * 点到点运动规划（梯形速度曲线）。
 * 给定目标位置，按最大速度 / 最大加速度规划并推进 position。
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float position;
    float velocity;
    float acceleration;

    float max_velocity;
    float max_acceleration;
    float max_jerk;

    /* 内部状态 */
    float target;
    bool active;
} motion_profile_t;

int motion_init(motion_profile_t *profile);
int motion_set_target(motion_profile_t *profile, float target);
int motion_update(motion_profile_t *profile, float dt);
bool motion_finished(const motion_profile_t *profile);

#ifdef __cplusplus
}
#endif

#endif /* MOTION_H */