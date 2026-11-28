/*
 * trajectory.c - 轨迹规划实现
 *
 * 梯形曲线：按 v²=2as 计算加减速距离，行程不足时降低峰值速度。
 * S 曲线：加速段由 3 个子段组成（jerk 上升 / 恒加速 / jerk 下降），
 *         峰值速度 vpeak 用二分法求解，使 2*s_acc(vpeak) == distance；
 *         若距离足够则 vpeak = vmax，剩余时间作为匀速段。
 * 采样均对时间做端点钳位，保证 t=t_total 时位置精确等于 distance。
 */

#include "trajectory.h"

#include <stddef.h>

#include <math.h>

#define TRAJ_EPS 1e-6f

static float clampf_local(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ============================== 梯形曲线 ============================== */

int trajectory_trap_plan(trajectory_trap_t *t, float distance,
                         float vmax, float acc, float dec)
{
    float d_acc_full;
    float d_dec_full;

    if (t == NULL || !(distance > 0.0f) || !(vmax > 0.0f) ||
        !(acc > 0.0f) || !(dec > 0.0f)) {
        return -1;
    }

    t->distance = distance;
    t->vmax = vmax;
    t->acc = acc;
    t->dec = dec;

    d_acc_full = vmax * vmax / (2.0f * acc);
    d_dec_full = vmax * vmax / (2.0f * dec);

    if (distance >= d_acc_full + d_dec_full) {
        t->vpeak = vmax;
        t->t_acc = vmax / acc;
        t->t_dec = vmax / dec;
        t->t_cruise = (distance - d_acc_full - d_dec_full) / vmax;
    } else {
        /* 加速与减速距离之和等于总行程：v²/(2a) + v²/(2d) = D */
        t->vpeak = sqrtf(2.0f * distance * acc * dec / (acc + dec));
        t->t_acc = t->vpeak / acc;
        t->t_dec = t->vpeak / dec;
        t->t_cruise = 0.0f;
    }

    t->t_total = t->t_acc + t->t_cruise + t->t_dec;
    return 0;
}

int trajectory_trap_sample(const trajectory_trap_t *t, float time,
                           float *pos, float *vel, float *acc)
{
    float tm;
    float p;
    float v;
    float a;

    if (t == NULL || !(t->t_total > 0.0f)) {
        return -1;
    }

    tm = clampf_local(time, 0.0f, t->t_total);

    if (tm <= 0.0f) {
        p = 0.0f;
        v = 0.0f;
        a = t->acc;
    } else if (tm >= t->t_total) {
        p = t->distance;
        v = 0.0f;
        a = 0.0f;
    } else if (tm < t->t_acc) {
        p = 0.5f * t->acc * tm * tm;
        v = t->acc * tm;
        a = t->acc;
    } else if (tm < t->t_acc + t->t_cruise) {
        float tc = tm - t->t_acc;
        p = 0.5f * t->acc * t->t_acc * t->t_acc + t->vpeak * tc;
        v = t->vpeak;
        a = 0.0f;
    } else {
        float left = t->t_total - tm;
        p = t->distance - 0.5f * t->dec * left * left;
        v = t->dec * left;
        a = -t->dec;
    }

    if (pos != NULL) *pos = p;
    if (vel != NULL) *vel = v;
    if (acc != NULL) *acc = a;
    return 0;
}

/* ============================== S 曲线 ============================== */

/*
 * 计算给定峰值速度下的加速段参数。
 * vp <= amax²/jerk 时只存在 jerk 上升段（三角形加速度）；
 * 否则为 3 段（jerk 上升 / 恒加速 / jerk 下降）。
 */
static void scurve_acc_layout(float vp, float amax, float jerk,
                              float *t1, float *t2, float *t3,
                              float *s_total)
{
    float ta;
    float s1;
    float v1;
    float s2;
    float v2;
    float s3;

    if (vp <= 0.0f) {
        *t1 = 0.0f;
        *t2 = 0.0f;
        *t3 = 0.0f;
        *s_total = 0.0f;
        return;
    }

    ta = amax / jerk;
    if (vp <= amax * ta) {
        /* 未达到最大加速度：单段 jerk */
        *t1 = sqrtf(2.0f * vp / jerk);
        *t2 = 0.0f;
        *t3 = 0.0f;
        *s_total = jerk * (*t1) * (*t1) * (*t1) / 6.0f;
        return;
    }

    *t1 = ta;
    *t2 = vp / amax - ta;
    *t3 = ta;

    s1 = jerk * (*t1) * (*t1) * (*t1) / 6.0f;
    v1 = jerk * (*t1) * (*t1) * 0.5f;
    s2 = v1 * (*t2) + 0.5f * amax * (*t2) * (*t2);
    v2 = v1 + amax * (*t2);
    s3 = v2 * (*t3) + 0.5f * amax * (*t3) * (*t3) -
         jerk * (*t3) * (*t3) * (*t3) / 6.0f;

    *s_total = s1 + s2 + s3;
}

/* 加速段内按时间 tau 采样（tau ∈ [0, t1+t2+t3]） */
static void scurve_acc_sample(float tau, float amax, float jerk,
                              float t1, float t2, float t3,
                              float *s, float *v, float *a)
{
    float s1;
    float v1;
    float s2;
    float v2;

    if (tau <= 0.0f) {
        *s = 0.0f;
        *v = 0.0f;
        *a = 0.0f;
        return;
    }

    if (tau <= t1) {
        *s = jerk * tau * tau * tau / 6.0f;
        *v = 0.5f * jerk * tau * tau;
        *a = jerk * tau;
        return;
    }

    if (t2 <= 0.0f) {
        /* 三角形加速度：只有一段，超出部分按加速度段结束处理 */
        *s = jerk * t1 * t1 * t1 / 6.0f;
        *v = 0.5f * jerk * t1 * t1;
        *a = jerk * t1;
        return;
    }

    s1 = jerk * t1 * t1 * t1 / 6.0f;
    v1 = 0.5f * jerk * t1 * t1;

    if (tau <= t1 + t2) {
        float tc = tau - t1;
        *s = s1 + v1 * tc + 0.5f * amax * tc * tc;
        *v = v1 + amax * tc;
        *a = amax;
        return;
    }

    {
        float td = tau - t1 - t2;
        if (td > t3) {
            td = t3;
        }
        s2 = v1 * t2 + 0.5f * amax * t2 * t2;
        v2 = v1 + amax * t2;
        *s = s1 + s2 + v2 * td + 0.5f * amax * td * td -
             jerk * td * td * td / 6.0f;
        *v = v2 + amax * td - 0.5f * jerk * td * td;
        *a = amax - jerk * td;
    }
}

int trajectory_scurve_plan(trajectory_scurve_t *t, float distance,
                           float vmax, float amax, float jerk)
{
    float t1;
    float t2;
    float t3;
    float s_acc;
    float lo;
    float hi;
    int i;

    if (t == NULL || !(distance > 0.0f) || !(vmax > 0.0f) ||
        !(amax > 0.0f) || !(jerk > 0.0f)) {
        return -1;
    }

    t->distance = distance;
    t->vmax = vmax;
    t->amax = amax;
    t->jerk = jerk;

    scurve_acc_layout(vmax, amax, jerk, &t1, &t2, &t3, &s_acc);
    if (2.0f * s_acc <= distance) {
        t->vpeak = vmax;
    } else {
        /* 二分求满足 2*s_acc(vp) == distance 的峰值速度 */
        lo = 0.0f;
        hi = vmax;
        for (i = 0; i < 64; i++) {
            float mid = 0.5f * (lo + hi);
            float st;
            scurve_acc_layout(mid, amax, jerk, &t1, &t2, &t3, &st);
            if (2.0f * st < distance) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        t->vpeak = 0.5f * (lo + hi);
    }

    scurve_acc_layout(t->vpeak, amax, jerk, &t->t1, &t->t2, &t->t3, &t->s_acc);
    t->t_acc = t->t1 + t->t2 + t->t3;
    t->t_cruise = (t->vpeak > TRAJ_EPS)
                  ? (distance - 2.0f * t->s_acc) / t->vpeak : 0.0f;
    if (t->t_cruise < 0.0f) {
        t->t_cruise = 0.0f;
    }
    t->t_total = 2.0f * t->t_acc + t->t_cruise;
    return 0;
}

int trajectory_scurve_sample(const trajectory_scurve_t *t, float time,
                             float *pos, float *vel, float *acc)
{
    float tm;
    float p;
    float v;
    float a;

    if (t == NULL || !(t->t_total > 0.0f)) {
        return -1;
    }

    tm = clampf_local(time, 0.0f, t->t_total);

    if (tm >= t->t_total) {
        p = t->distance;
        v = 0.0f;
        a = 0.0f;
    } else if (tm < t->t_acc) {
        scurve_acc_sample(tm, t->amax, t->jerk, t->t1, t->t2, t->t3,
                          &p, &v, &a);
    } else if (tm < t->t_acc + t->t_cruise) {
        p = t->s_acc + t->vpeak * (tm - t->t_acc);
        v = t->vpeak;
        a = 0.0f;
    } else {
        /* 减速段与加速段时间对称：位置 = D - s_acc(t_acc - tau) */
        float tau = t->t_acc - (tm - t->t_acc - t->t_cruise);
        float s;
        scurve_acc_sample(tau, t->amax, t->jerk, t->t1, t->t2, t->t3,
                          &s, &v, &a);
        p = t->distance - s;
        a = -a;
    }

    if (pos != NULL) *pos = p;
    if (vel != NULL) *vel = v;
    if (acc != NULL) *acc = a;
    return 0;
}

/* ============================== 多轴直线插补 ============================== */

int trajectory_linear_plan(trajectory_trap_t *t, float vmax,
                           float acc, float dec)
{
    return trajectory_trap_plan(t, 1.0f, vmax, acc, dec);
}

int trajectory_linear_sample(const trajectory_trap_t *profile, float time,
                             const float *start, const float *end,
                             uint8_t axis_count, float *out)
{
    float s;
    uint8_t i;

    if (profile == NULL || start == NULL || end == NULL ||
        out == NULL || axis_count == 0U) {
        return -1;
    }
    if (trajectory_trap_sample(profile, time, &s, NULL, NULL) != 0) {
        return -1;
    }
    for (i = 0; i < axis_count; i++) {
        out[i] = start[i] + (end[i] - start[i]) * s;
    }
    return 0;
}
