#ifndef NAVIGATION_H
#define NAVIGATION_H

/*
 * 机器人导航（路径规划）公共数据结构与算法声明。
 * 统一采用占据栅格地图（0=可通行，非0=障碍），路径以栅格 index 序列表示。
 * 全部算法使用固定大小工作内存，无动态分配，适合 MCU。
 *
 * 模块职责：在二维栅格图上求解从起点到终点的可行路径，提供多种取舍不同的算法：
 *   - Dijkstra：只按代价扩展，结果最优但最慢（无启发）；
 *   - A*：八邻域 + 八向（octile）启发，速度与最优性折中，是默认选择；
 *   - Theta*：在 A* 基础上用视线检查选择祖父节点，路径更接近任意角度、转折更少；
 *   - RRT：随机采样树，适合高维/复杂空间，路径不保证最优；
 *   - D* Lite：反向增量搜索，环境障碍变化后可复用已有搜索状态快速重规划。
 *
 * 坐标与索引约定：
 *   - 栅格 (x, y) 中 x 为列、y 为行，(0,0) 位于左上角（按行优先存储）；
 *   - 线性索引 idx = y * width + x；
 *   - 路径节点保存的是栅格线性索引，顺序为“起点 → 终点”。
 *
 * 返回值约定（各规划函数）：
 *   0  成功，path 已填充；
 *   -1 参数非法（map/path/cells 为 NULL）；
 *   -2 地图规模超过 NAVIGATION_MAX_CELLS；
 *   -3 起点或终点不可通行；
 *   -4 不存在可行路径；
 *   -5 工作内存/节点数达到上限（仅 RRT）。
 *
 * 注意事项：
 *   - 所有算法共用 .c 中的静态工作数组，同一时刻只能有一个搜索在进行，非线程安全；
 *   - 路径长度若等于 NAVIGATION_MAX_NODES，说明可能被截断，调用方需自行判断；
 *   - D* Lite 的搜索状态为全局静态变量，navigation_dstar_lite 会覆盖上一次的状态。
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NAVIGATION_MAX_CELLS  4096   /* 最大栅格数（默认支持到 64x64） */
#define NAVIGATION_MAX_NODES  4096   /* 路径最大节点数 */

/* 栅格地图视图：只引用外部数据，不拥有 cells 内存 */
typedef struct {
    uint16_t width;         /* 列数 */
    uint16_t height;        /* 行数 */
    const uint8_t *cells;   /* 长度 = width*height，0 可通行，非 0 障碍 */
} navigation_grid_map_t;

/* 规划结果：线性索引序列，nodes[0] 为起点、nodes[len-1] 为终点 */
typedef struct {
    uint16_t nodes[NAVIGATION_MAX_NODES];   /* 路径节点（栅格 index） */
    uint16_t len;                        /* 节点数；可能达到上限而被截断 */
} navigation_path_t;

/*
 * 栅格坐标转线性索引（行优先）。
 * @param x 列号；y 行号；width 地图宽度
 * @return idx = y*width + x
 * @note 不做范围检查，调用方需保证 x < width。
 */
uint32_t navigation_idx(uint16_t x, uint16_t y, uint16_t width);

/*
 * 判断栅格是否可通行。
 * @return true 表示在界内且 cells 值为 0；越界/map 为 NULL/cells 为 NULL 均返回 false
 */
bool navigation_passable(const navigation_grid_map_t *map, uint16_t x, uint16_t y);

/*
 * Dijkstra：加权最短（无启发）。
 * @param sx,sy 起点栅格；gx,gy 终点栅格；path 输出路径
 * @return 0 成功；负值见文件头返回值约定
 * @note 八邻域，直行代价 10、对角 14（近似 √2 的整数化）。
 */
int navigation_dijkstra(const navigation_grid_map_t *map,
                     uint16_t sx, uint16_t sy, uint16_t gx, uint16_t gy,
                     navigation_path_t *path);

/*
 * A*：八邻域 + 八向启发。
 * @return 0 成功；负值见文件头返回值约定
 * @note 启发函数为 octile 距离，与真实代价同尺度，可保证最优性。
 */
int navigation_astar(const navigation_grid_map_t *map,
                  uint16_t sx, uint16_t sy, uint16_t gx, uint16_t gy,
                  navigation_path_t *path);

/*
 * Theta*：任意角路径（A* + 视线父节点）。
 * @return 0 成功；负值见文件头返回值约定
 * @note 当“祖父节点→当前邻居”视线通畅时直接以祖父为父节点，减少路径拐点。
 */
int navigation_thetastar(const navigation_grid_map_t *map,
                      uint16_t sx, uint16_t sy, uint16_t gx, uint16_t gy,
                      navigation_path_t *path);

/*
 * RRT：快速探索随机树（采样规划，路径为折线）。
 * @return 0 成功；负值见文件头返回值约定（-5 表示树节点耗尽）
 * @note 采用固定迭代次数与固定伪随机种子，结果可复现但非最优。
 */
int navigation_rrt(const navigation_grid_map_t *map,
                uint16_t sx, uint16_t sy, uint16_t gx, uint16_t gy,
                navigation_path_t *path);

/*
 * D* Lite：动态环境重规划（支持障碍变化后重新求解）。
 * @return 0 成功；负值见文件头返回值约定
 * @note 会重建全局搜索状态，并保存 map 指针供 navigation_dstar_replan 复用。
 */
int navigation_dstar_lite(const navigation_grid_map_t *map,
                       uint16_t sx, uint16_t sy, uint16_t gx, uint16_t gy,
                       navigation_path_t *path);

/*
 * D* Lite 障碍变化后增量重规划（需先调用 navigation_dstar_lite 建立状态）。
 * @param map  新的地图视图（cells 内容已被外部修改）；传 NULL 表示沿用上次 map
 * @param path 输出新的路径
 * @return 0 成功；-1 参数非法或尚未建立搜索状态；-4 无可行路径
 */
int navigation_dstar_replan(const navigation_grid_map_t *map, navigation_path_t *path);

#ifdef __cplusplus
}
#endif

#endif /* NAVIGATION_H */