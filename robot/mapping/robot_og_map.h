#ifndef ROBOT_OG_MAP_H
#define ROBOT_OG_MAP_H

/*
 * 2D 占据栅格建图（Occupancy Grid Mapping，SLAM 的建图部分）。
 * 已知机器人位姿与传感器测距，用 log-odds 更新每格占据概率；
 * 最终二值化导出 0/1 栅格地图供 robot_nav 导航算法使用。
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 机器人位姿（世界坐标系，米 + 弧度） */
typedef struct {
    float x;
    float y;
    float theta;
} robot_pose_t;

/* 单束测距点（相对机器人朝向） */
typedef struct {
    float angle;   /* 相对角度（弧度） */
    float range;   /* 距离（米），<=0 或 >=max_range 视为未命中 */
} robot_scan_point_t;

/* 占据栅格地图 */
typedef struct {
    uint16_t width;
    uint16_t height;
    float resolution;         /* 米/格 */
    float origin_x;           /* 格(0,0) 左下角世界 x（米） */
    float origin_y;
    int16_t *cells;           /* log-odds 值（×100），外部提供 width*height 数组 */
} robot_og_map_t;

int robot_og_map_init(robot_og_map_t *map, uint16_t width, uint16_t height,
                    float resolution, float origin_x, float origin_y, int16_t *cells);

/* 世界坐标 -> 栅格坐标；超出范围返回非 0 */
int iot_og_world_to_cell(const robot_og_map_t *map, float wx, float wy,
                         int16_t *cx, int16_t *cy);

/* 用一次扫描更新地图：points 为各束测距，max_range 为量程上限 */
int robot_og_map_update(robot_og_map_t *map, const robot_pose_t *pose,
                      const robot_scan_point_t *points, uint16_t count, float max_range);

/* 读取某格占用概率 [0,1] */
float robot_og_map_probability(const robot_og_map_t *map, int16_t cx, int16_t cy);

/* 导出二值化地图（概率 > threshold 视为障碍），out 长度 >= width*height */
int robot_og_map_export(const robot_og_map_t *map, uint8_t *out, float threshold);

#ifdef __cplusplus
}
#endif

#endif /* ROBOT_OG_MAP_H */