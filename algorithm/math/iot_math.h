#ifndef IOT_MATH_H
#define IOT_MATH_H

/*
 * 数学基础（float）。算法库独立于 Core 与 HAL。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

float iot_math_abs(float x);
float iot_math_min(float a, float b);
float iot_math_max(float a, float b);
float iot_math_clamp(float x, float min, float max);

float iot_math_map(float x, float in_min, float in_max,
                   float out_min, float out_max);
float iot_math_lerp(float a, float b, float t);

float iot_math_deg_to_rad(float deg);
float iot_math_rad_to_deg(float rad);

float iot_math_wrap_angle(float angle);   /* 归一到 [-180, 180) */
float iot_math_deadband(float value, float threshold);
float iot_math_sign(float value);

float iot_math_sqrt(float value);
float iot_math_pow(float base, float exponent);

#ifdef __cplusplus
}
#endif

#endif /* IOT_MATH_H */