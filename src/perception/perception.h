#ifndef PERCEPTION_H
#define PERCEPTION_H

/*
 * perception.h - 测距传感器统一扫描抽象
 *
 * 把激光雷达、超声、ToF 等传感器的单次测量统一为 (角度, 距离) 点，
 * 存入调用者提供的环形缓冲区，并可转换到机器人坐标系/世界坐标系。
 *
 * 角度约定：相对机器人朝向，逆时针为正（弧度）；距离单位为米。
 * 无动态内存，缓冲区由调用者提供。
 */

#include <stdint.h>
#include "kinematics.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 单束测距点 */
typedef struct {
    float angle;    /* 相对机器人朝向的角度（弧度） */
    float range;    /* 距离（米），<=0 视为无效点 */
} scan_point_t;

/* 扫描环形缓冲 */
typedef struct {
    scan_point_t *buf;   /* 外部提供的缓冲区 */
    uint16_t capacity;   /* 缓冲容量（点数） */
    uint16_t head;       /* 下一个写入位置 */
    uint16_t count;      /* 当前有效点数 */
} scan_buffer_t;

/*
 * 初始化扫描缓冲。
 * 返回：0 成功；-1 参数非法。
 */
int scan_init(scan_buffer_t *scan, scan_point_t *buf, uint16_t capacity);

/* 清空缓冲 */
void scan_clear(scan_buffer_t *scan);

/*
 * 追加一帧扫描点；容量不足时覆盖最旧的点。
 * 返回：实际写入的点数；参数非法返回 0。
 */
uint16_t scan_push(scan_buffer_t *scan, const scan_point_t *points, uint16_t count);

/* 当前有效点数 */
uint16_t scan_count(const scan_buffer_t *scan);

/*
 * 按时间顺序读取：index 0 为最旧的点。
 * 返回：0 成功；-1 参数非法或越界。
 */
int scan_get(const scan_buffer_t *scan, uint16_t index, scan_point_t *out);

/* 单点转机器人坐标系（x 向前，y 向左） */
int scan_to_cartesian(const scan_point_t *point, float *x, float *y);

/*
 * 批量转换到世界坐标系：先按角度转机体坐标，再叠加位姿。
 * xs/ys 长度必须 >= count。
 * 返回：0 成功；-1 参数非法。
 */
int scan_transform_world(const pose2d_t *pose, const scan_point_t *points,
                         uint16_t count, float *xs, float *ys);

/*
 * 在角度区间内找最近的有效距离。
 * 返回：0 找到（range 输出）；-1 参数非法或区间内无有效点。
 */
int scan_nearest(const scan_point_t *points, uint16_t count,
                 float min_angle, float max_angle, float *range);

#ifdef __cplusplus
}
#endif

#endif /* PERCEPTION_H */
