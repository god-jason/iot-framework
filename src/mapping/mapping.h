#ifndef MAPPING_H
#define MAPPING_H

/*
 * 2D 占据栅格建图（Occupancy Grid Mapping，SLAM 的建图部分）。
 * 已知机器人位姿与传感器测距，用 log-odds 更新每格占据概率；
 * 最终二值化导出 0/1 栅格地图供 navigation 导航算法使用。
 *
 * 模块职责：把一帧测距数据（机器人坐标系下的角度+距离）投影到世界坐标系栅格，
 * 沿射线把途经格子标记为“空闲”、把命中格子标记为“占据”，并累积概率。
 *
 * 数值约定：
 *   - 每格保存 int16_t 的 log-odds 值，单位 1/100（即 85 表示 log-odds 0.85）；
 *   - 初值 0 表示未知（概率 0.5），正值偏向占据、负值偏向空闲；
 *   - 取值范围被限制在 [-400, 400]，防止饱和后无法反向修正。
 *
 * 注意事项：
 *   - cells 数组由调用方提供，长度必须 >= width*height，框架不分配内存；
 *   - 所有坐标单位为米/弧度，栅格索引为 (0,0) 在左下角；
 *   - 非线程安全，单帧更新应在同一任务中串行调用。
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 机器人位姿（世界坐标系，米 + 弧度） */
typedef struct {
    float x;        /* 世界坐标 x（米） */
    float y;        /* 世界坐标 y（米） */
    float theta;    /* 朝向角（弧度），0 指向世界 +x 轴 */
} mapping_pose_t;

/* 单束测距点（相对机器人朝向） */
typedef struct {
    float angle;   /* 相对角度（弧度），实际射线方向 = pose.theta + angle */
    float range;   /* 距离（米），<=0 或 >=max_range 视为未命中 */
} mapping_scan_point_t;

/* 占据栅格地图 */
typedef struct {
    uint16_t width;           /* 栅格列数（x 方向） */
    uint16_t height;          /* 栅格行数（y 方向） */
    float resolution;         /* 米/格 */
    float origin_x;           /* 格(0,0) 左下角世界 x（米） */
    float origin_y;           /* 格(0,0) 左下角世界 y（米） */
    int16_t *cells;           /* log-odds 值（×100），外部提供 width*height 数组 */
} mapping_og_map_t;

/*
 * 初始化地图元数据，并把全部栅格置 0（未知，概率 0.5）。
 * @param map        地图对象
 * @param width      列数，必须 >0
 * @param height     行数，必须 >0
 * @param resolution 米/格，必须 >0
 * @param origin_x   左下角世界 x（米）
 * @param origin_y   左下角世界 y（米）
 * @param cells      外部提供的 int16_t 数组，长度 >= width*height
 * @return 0 成功；-1 参数非法
 */
int mapping_og_map_init(mapping_og_map_t *map, uint16_t width, uint16_t height,
                    float resolution, float origin_x, float origin_y, int16_t *cells);

/*
 * 世界坐标 -> 栅格坐标。
 * 使用向下取整（截断）把连续坐标映射到所在格子。
 * @param cx 输出列索引
 * @param cy 输出行索引
 * @return 0 成功；-1 表示 map 为 NULL 或坐标落在地图外
 */
int mapping_world_to_cell(const mapping_og_map_t *map, float wx, float wy,
                         int16_t *cx, int16_t *cy);

/*
 * 用一次扫描更新地图：points 为各束测距，max_range 为量程上限。
 * 对每束射线：起点到终点之间标记空闲（-OG_L_FREE），终点按是否命中标记占据/空闲。
 * @param pose      当前机器人位姿
 * @param points    测距点数组，不可为 NULL
 * @param count     测距点个数
 * @param max_range 量程上限（米），range>=max_range 视为未命中
 * @return 0 成功；-1 参数为 NULL；-2 机器人位姿落在地图外
 */
int mapping_og_map_update(mapping_og_map_t *map, const mapping_pose_t *pose,
                      const mapping_scan_point_t *points, uint16_t count, float max_range);

/*
 * 读取某格占用概率 [0,1]。
 * @return 概率值；越界或 map 为 NULL 时返回 0.5（未知）
 */
float mapping_og_map_probability(const mapping_og_map_t *map, int16_t cx, int16_t cy);

/*
 * 导出二值化地图（概率 > threshold 视为障碍），out 长度 >= width*height。
 * @param threshold 判定阈值（0~1）
 * @param out       输出缓冲，每格 0（可通行）或 1（障碍）
 * @return 0 成功；-1 参数为 NULL
 * @note 未知格概率为 0.5，阈值高于 0.5 时未知格会被判为可通行。
 */
int mapping_og_map_export(const mapping_og_map_t *map, uint8_t *out, float threshold);

#ifdef __cplusplus
}
#endif

#endif /* MAPPING_H */