#include "iot_math.h"
#include <math.h>

float iot_math_abs(float x) { return fabsf(x); }
float iot_math_min(float a, float b) { return (a < b) ? a : b; }
float iot_math_max(float a, float b) { return (a > b) ? a : b; }

float iot_math_clamp(float x, float min, float max)
{
    if (x < min) return min;
    if (x > max) return max;
    return x;
}

float iot_math_map(float x, float in_min, float in_max,
                   float out_min, float out_max)
{
    if (in_max == in_min) {
        return out_min;
    }
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

float iot_math_lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

float iot_math_deg_to_rad(float deg) { return deg * 0.017453292519943295f; }
float iot_math_rad_to_deg(float rad) { return rad * 57.29577951308232f; }

float iot_math_wrap_angle(float angle)
{
    while (angle >= 180.0f) {
        angle -= 360.0f;
    }
    while (angle < -180.0f) {
        angle += 360.0f;
    }
    return angle;
}

float iot_math_deadband(float value, float threshold)
{
    if (fabsf(value) < threshold) {
        return 0.0f;
    }
    return value;
}

float iot_math_sign(float value)
{
    if (value > 0.0f) return 1.0f;
    if (value < 0.0f) return -1.0f;
    return 0.0f;
}

float iot_math_sqrt(float value) { return sqrtf(value); }
float iot_math_pow(float base, float exponent) { return powf(base, exponent); }