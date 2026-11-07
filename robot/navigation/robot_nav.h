#ifndef ROBOT_NAV_H
#define ROBOT_NAV_H

/*
 * 机器人导航（路径规划）公共数据结构与算法声明。
 * 统一采用占据栅格地图（0=可通行，非0=障碍），路径以栅格 index 序列表示。
 * 全部算法使用固定大小工作内存，无动态分配，适合 MCU。
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBOT_NAV_MAX_CELLS  4096   /* 最大栅格数（默认支持到 64x64） */
#define ROBOT_NAV_MAX_NODES  4096   /* 路径最大节点数 */

typedef struct {
    uint16_t width;
    uint16_t height;
    const uint8_t *cells;   /* 长度 = width*height，0 可通行，非 0 障碍 */
} robot_grid_map_t;

typedef struct {
    uint16_t nodes[ROBOT_NAV_MAX_NODES];   /* 路径节点（栅格 index） */
    uint16_t len;                        /* 节点数 */
} robot_path_t;

/* 坐标 -> index */
uint32_t robot_nav_idx(uint16_t x, uint16_t y, uint16_t width);

/* 判断栅格是否可通行 */
bool robot_nav_passable(const robot_grid_map_t *map, uint16_t x, uint16_t y);

/* Dijkstra：加权最短（无启发） */
int robot_nav_dijkstra(const robot_grid_map_t *map,
                     uint16_t sx, uint16_t sy, uint16_t gx, uint16_t gy,
                     robot_path_t *path);

/* A*：八邻域 + 八向启发 */
int robot_nav_astar(const robot_grid_map_t *map,
                  uint16_t sx, uint16_t sy, uint16_t gx, uint16_t gy,
                  robot_path_t *path);

/* Theta*：任意角路径（A* + 视线父节点） */
int robot_nav_thetastar(const robot_grid_map_t *map,
                      uint16_t sx, uint16_t sy, uint16_t gx, uint16_t gy,
                      robot_path_t *path);

/* RRT：快速探索随机树（采样规划，路径为折线） */
int robot_nav_rrt(const robot_grid_map_t *map,
                uint16_t sx, uint16_t sy, uint16_t gx, uint16_t gy,
                robot_path_t *path);

/* D* Lite：动态环境重规划（支持障碍变化后重新求解） */
int robot_nav_dstar_lite(const robot_grid_map_t *map,
                       uint16_t sx, uint16_t sy, uint16_t gx, uint16_t gy,
                       robot_path_t *path);

/* D* Lite 障碍变化后增量重规划（需先调用 robot_nav_dstar_lite 建立状态） */
int robot_nav_dstar_replan(const robot_grid_map_t *map, robot_path_t *path);

#ifdef __cplusplus
}
#endif

#endif /* ROBOT_NAV_H */