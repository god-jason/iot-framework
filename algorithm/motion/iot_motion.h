#ifndef IOT_MOTION_H
#define IOT_MOTION_H

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
} iot_motion_profile_t;

int iot_motion_init(iot_motion_profile_t *profile);
int iot_motion_set_target(iot_motion_profile_t *profile, float target);
int iot_motion_update(iot_motion_profile_t *profile, float dt);
bool iot_motion_finished(const iot_motion_profile_t *profile);

#ifdef __cplusplus
}
#endif

#endif /* IOT_MOTION_H */