#ifndef INTERP_H
#define INTERP_H

/*
 * 插值：线性 / 查表（线性）/ 三次（Catmull-Rom，均匀采样）。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

float interp_linear(float x, float x0, float y0, float x1, float y1);

int interp_lookup(float x, const float *x_table, const float *y_table,
                      uint16_t count, float *output);

float interp_cubic(float x, const float *points, uint16_t count);

#ifdef __cplusplus
}
#endif

#endif /* INTERP_H */