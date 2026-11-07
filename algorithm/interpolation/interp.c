#include "interp.h"

static float interp_clamp(float x, float lo, float hi)
{
    return (x < lo) ? lo : ((x > hi) ? hi : x);
}

float interp_linear(float x, float x0, float y0, float x1, float y1)
{
    if (x0 == x1) {
        return y0;
    }
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
}

int interp_lookup(float x, const float *x_table, const float *y_table,
                      uint16_t count, float *output)
{
    uint16_t i;

    if (x_table == NULL || y_table == NULL || output == NULL || count < 2) {
        return -1;
    }
    if (x <= x_table[0]) {
        *output = y_table[0];
        return 0;
    }
    if (x >= x_table[count - 1]) {
        *output = y_table[count - 1];
        return 0;
    }
    for (i = 0; i < count - 1; i++) {
        if (x >= x_table[i] && x <= x_table[i + 1]) {
            *output = interp_linear(x, x_table[i], y_table[i],
                                        x_table[i + 1], y_table[i + 1]);
            return 0;
        }
    }
    return -1;
}

/* Catmull-Rom 三次插值：points 为均匀采样 y 值（步长 1），x 为连续索引 */
float interp_cubic(float x, const float *points, uint16_t count)
{
    int i;
    float t, p0, p1, p2, p3;
    float a, b, c, d;

    if (points == NULL || count < 4) {
        return 0.0f;
    }
    i = (int)x;
    if (i < 0) {
        i = 0;
    }
    if (i > (int)count - 2) {
        i = (int)count - 2;
    }
    t = interp_clamp(x - (float)i, 0.0f, 1.0f);

    p1 = points[i];
    p2 = points[i + 1];
    p0 = (i > 0) ? points[i - 1] : p1;
    p3 = (i < (int)count - 2) ? points[i + 2] : p2;

    /* Catmull-Rom 基 */
    a = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
    b = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
    c = -0.5f * p0 + 0.5f * p2;
    d = p1;

    return ((a * t + b) * t + c) * t + d;
}