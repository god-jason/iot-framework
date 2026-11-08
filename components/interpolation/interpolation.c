/*
 * 插值实现：两点线性插值、表驱动分段线性查表、Catmull-Rom 三次插值。
 *
 * 数据流：调用方提供已知点（x_table/y_table 或均匀采样的 points），
 *         输入任意 x，返回对应的 y 估计值。
 * 边界：
 *   - 线性插值在 x0 == x1 时直接返回 y0，避免除零；
 *   - 查表在区间外采用端点钳位（不外推）；
 *   - 三次插值两端通过复制端点作为虚拟控制点，保证边界连续。
 * 所有接口均为无状态纯函数，可直接在中断/任务上下文中调用。
 */
#include "interpolation.h"

/* 将 x 限制在 [lo, hi] 内（参数区间钳位） */
static float interpolation_clamp(float x, float lo, float hi)
{
    return (x < lo) ? lo : ((x > hi) ? hi : x);
}

/*
 * 两点线性插值。
 * 参数：(x0,y0) 起点；(x1,y1) 终点；x 待求点。
 * 返回：直线在 x 处的值；x0 == x1 时返回 y0。
 * 说明：不做区间钳位，x 超出 [x0,x1] 时为线性外推结果。
 */
float interpolation_linear(float x, float x0, float y0, float x1, float y1)
{
    if (x0 == x1) {
        return y0;
    }
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
}

/*
 * 查表插值（分段线性）。
 * 参数：x 输入值；x_table 升序 x 坐标数组；y_table 对应 y 值数组；
 *       count 表格点数（>= 2）；output 输出结果。
 * 返回：0 成功；-1 参数非法（空指针 / 点数不足）。
 * 说明：x 落在表格端点之外时按最近端点取值（钳位，不做外推）；
 *       若表格非单调，仅返回首个命中区间，行为未定义。
 */
int interpolation_lookup(float x, const float *x_table, const float *y_table,
                      uint16_t count, float *output)
{
    uint16_t i;

    if (x_table == NULL || y_table == NULL || output == NULL || count < 2) {
        return -1;
    }
    /* 左端钳位：低于首点直接取首值 */
    if (x <= x_table[0]) {
        *output = y_table[0];
        return 0;
    }
    /* 右端钳位：高于末点直接取末值 */
    if (x >= x_table[count - 1]) {
        *output = y_table[count - 1];
        return 0;
    }
    /* 线性扫描定位区间并做两点插值 */
    for (i = 0; i < count - 1; i++) {
        if (x >= x_table[i] && x <= x_table[i + 1]) {
            *output = interpolation_linear(x, x_table[i], y_table[i],
                                        x_table[i + 1], y_table[i + 1]);
            return 0;
        }
    }
    return -1;
}

/*
 * Catmull-Rom 三次插值：points 为均匀采样 y 值（步长 1），x 为连续索引。
 * 参数：x 连续索引（如 x=2.5 表示第 2、3 点之间中点）；
 *       points 均匀采样序列（相邻点间隔为 1）；count 点数（>= 4）。
 * 返回：插值结果；参数非法时返回 0。
 * 说明：取 p1=points[i]、p2=points[i+1] 为插值段，p0/p3 为相邻控制点，
 *       边界处无邻点则用端点自身复制。曲线经过 p1、p2（C1 连续），
 *       相比线性插值更平滑，适合传感器标定表、轨迹平滑等场景。
 */
float interpolation_cubic(float x, const float *points, uint16_t count)
{
    int i;
    float t, p0, p1, p2, p3;
    float a, b, c, d;

    if (points == NULL || count < 4) {
        return 0.0f;
    }
    /* 定位所在区间索引 i，限制在 [0, count-2] 以保证 i+1 合法 */
    i = (int)x;
    if (i < 0) {
        i = 0;
    }
    if (i > (int)count - 2) {
        i = (int)count - 2;
    }
    /* t 为区间内归一化位置，钳位到 [0,1] */
    t = interpolation_clamp(x - (float)i, 0.0f, 1.0f);

    p1 = points[i];
    p2 = points[i + 1];
    /* 边界处理：左端复制 p1，右端复制 p2 */
    p0 = (i > 0) ? points[i - 1] : p1;
    p3 = (i < (int)count - 2) ? points[i + 2] : p2;

    /* Catmull-Rom 基 */
    /* 展开多项式系数：y(t) = ((a*t + b)*t + c)*t + d */
    a = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
    b = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
    c = -0.5f * p0 + 0.5f * p2;
    d = p1;

    /* 按 Horner 法求值 */
    return ((a * t + b) * t + c) * t + d;
}
