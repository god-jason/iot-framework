#include "iot_motion.h"

#include <math.h>

int iot_motion_init(iot_motion_profile_t *profile)
{
    if (profile == NULL) {
        return -1;
    }
    profile->position = 0.0f;
    profile->velocity = 0.0f;
    profile->acceleration = 0.0f;
    profile->max_velocity = 1.0f;
    profile->max_acceleration = 1.0f;
    profile->max_jerk = 0.0f;
    profile->target = 0.0f;
    profile->active = false;
    return 0;
}

int iot_motion_set_target(iot_motion_profile_t *profile, float target)
{
    if (profile == NULL) {
        return -1;
    }
    profile->target = target;
    profile->active = true;
    return 0;
}

int iot_motion_update(iot_motion_profile_t *profile, float dt)
{
    float dir, dist, stop_dist;
    float a;

    if (profile == NULL || dt <= 0.0f || !profile->active) {
        return -1;
    }
    a = profile->max_acceleration;
    dir = (profile->target > profile->position) ? 1.0f : -1.0f;
    dist = fabsf(profile->target - profile->position);

    /* 减速距离：以当前速度减到 0 需要的距离 */
    stop_dist = (profile->velocity * profile->velocity) / (2.0f * a);

    if (dist <= stop_dist) {
        /* 进入减速段 */
        profile->velocity -= a * dt;
        if (profile->velocity < 0.0f) {
            profile->velocity = 0.0f;
        }
    } else {
        /* 加速 / 匀速段 */
        if (profile->velocity < profile->max_velocity) {
            profile->velocity += a * dt;
            if (profile->velocity > profile->max_velocity) {
                profile->velocity = profile->max_velocity;
            }
        }
    }

    profile->position += dir * profile->velocity * dt;
    profile->acceleration = (profile->velocity > 0.0f) ? a * dir : 0.0f;

    /* 到达判定 */
    if ((dir > 0.0f && profile->position >= profile->target) ||
        (dir < 0.0f && profile->position <= profile->target)) {
        profile->position = profile->target;
        profile->velocity = 0.0f;
        profile->acceleration = 0.0f;
        profile->active = false;
    }
    return 0;
}

bool iot_motion_finished(const iot_motion_profile_t *profile)
{
    return (profile == NULL) || !profile->active;
}