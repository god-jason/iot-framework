#ifndef INTERPOLATION_H
#define INTERPOLATION_H

/*
 * 插值：线性 / 查表（线性）/ 三次（Catmull-Rom，均匀采样）。
 *
 * 无状态纯函数库，不分配内存；用于传感器标定曲线、设定值映射等场景。
 * 查表接口要求 x_table 单调递增，区间外采用端点钳位（不外推）。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 两点线性插值。
 * 参数：(x0,y0)、(x1,y1) 已知点；x 待求点。
 * 返回：x 处的插值结果；x0 == x1 时返回 y0（避免除零）。
 */
float interpolation_linear(float x, float x0, float y0, float x1, float y1);

/*
 * 分段线性查表插值。
 * 参数：x 输入值；x_table 升序横坐标；y_table 纵坐标；count 点数(>=2)；
 *       output 结果输出（非空）。
 * 返回：0 成功；-1 参数非法。
 */
int interpolation_lookup(float x, const float *x_table, const float *y_table,
                      uint16_t count, float *output);

/*
 * Catmull-Rom 三次插值。
 * 参数：x 连续索引（相邻采样点间隔为 1）；points 均匀采样值；
 *       count 采样点数（>= 4）。
 * 返回：插值结果；参数非法时返回 0。
 * 注意：边界处用端点自身作为虚拟控制点，曲线经过 points[i]、points[i+1]。
 */
float interpolation_cubic(float x, const float *points, uint16_t count);

#ifdef __cplusplus
}
#endif

#endif /* INTERPOLATION_H */
