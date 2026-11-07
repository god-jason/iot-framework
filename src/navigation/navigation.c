/* 导航公共实现：栅格索引/通行判断、A* 核心（供 Dijkstra / A-star / Theta-star 复用）。 */
#include "navigation.h"

#include <string.h>

/* ============ 基础工具 ============ */

uint32_t navigation_idx(uint16_t x, uint16_t y, uint16_t width)
{
    return (uint32_t)y * width + x;
}

bool navigation_passable(const navigation_grid_map_t *map, uint16_t x, uint16_t y)
{
    if (map == NULL || map->cells == NULL) {
        return false;
    }
    if (x >= map->width || y >= map->height) {
        return false;
    }
    return map->cells[(uint32_t)y * map->width + x] == 0;
}

static int32_t nav_abs(int32_t v) { return (v < 0) ? -v : v; }

static uint32_t nav_octile(uint16_t ax, uint16_t ay, uint16_t bx, uint16_t by)
{
    int32_t dx = nav_abs((int32_t)ax - bx);
    int32_t dy = nav_abs((int32_t)ay - by);
    if (dx > dy) {
        return (uint32_t)(14 * dy + 10 * (dx - dy));
    }
    return (uint32_t)(14 * dx + 10 * (dy - dx));
}

/* 启发：八向距离；use_h 为假时返回 0（Dijkstra） */
static uint32_t nav_h(uint16_t x, uint16_t y, uint16_t gx, uint16_t gy, bool use_h)
{
    if (!use_h) {
        return 0;
    }
    return nav_octile(x, y, gx, gy);
}

/* Bresenham 视线判断：a 到 b 之间所有栅格均可通行 */
static bool nav_line_of_sight(const navigation_grid_map_t *map, int32_t a, int32_t b)
{
    int32_t w = map->width;
    int32_t x0 = a % w, y0 = a / w;
    int32_t x1 = b % w, y1 = b / w;
    int32_t dx = nav_abs(x1 - x0), dy = nav_abs(y1 - y0);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx - dy;
    int32_t x = x0, y = y0;

    while (x != x1 || y != y1) {
        int32_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx)  { err += dx; y += sy; }
        if (x == x1 && y == y1) {
            break;
        }
        if (!navigation_passable(map, (uint16_t)x, (uint16_t)y)) {
            return false;
        }
    }
    return true;
}

/* ============ A* 核心（Dijkstra / A-star / Theta-star 共用） ============ */

#define NAV_INF 0xFFFFFFFFu

static uint32_t s_g[NAVIGATION_MAX_CELLS];
static int32_t  s_parent[NAVIGATION_MAX_CELLS];
static uint8_t  s_closed[NAVIGATION_MAX_CELLS];
static uint32_t s_open[NAVIGATION_MAX_CELLS];
static uint8_t  s_in_open[NAVIGATION_MAX_CELLS];
static uint16_t s_open_count;

typedef struct { int16_t dx, dy; uint32_t cost; } nav_dir_t;
static const nav_dir_t nav_dirs[8] = {
    { 1, 0, 10}, {-1, 0, 10}, {0, 1, 10}, {0, -1, 10},
    { 1, 1, 14}, { 1,-1, 14}, {-1, 1, 14}, {-1,-1, 14}
};

static int nav_build_path(const navigation_grid_map_t *map, int32_t start, int32_t goal,
                          navigation_path_t *path)
{
    uint16_t temp[NAVIGATION_MAX_NODES];
    uint16_t len = 0;
    int32_t cur = goal;
    (void)map;

    while (cur >= 0 && len < NAVIGATION_MAX_NODES) {
        temp[len++] = (uint16_t)cur;
        if (cur == start) {
            break;
        }
        cur = s_parent[cur];
    }
    if (cur != start) {
        return -1;
    }
    for (uint16_t i = 0; i < len; i++) {
        path->nodes[i] = temp[len - 1 - i];
    }
    path->len = len;
    return 0;
}

static int nav_astar_search(const navigation_grid_map_t *map,
                            uint16_t sx, uint16_t sy, uint16_t gx, uint16_t gy,
                            bool use_h, bool use_los, navigation_path_t *path)
{
    uint32_t start, goal, total;

    if (map == NULL || path == NULL || map->cells == NULL) {
        return -1;
    }
    start = navigation_idx(sx, sy, map->width);
    goal  = navigation_idx(gx, gy, map->width);
    total = (uint32_t)map->width * map->height;
    if (total > NAVIGATION_MAX_CELLS) {
        return -2;
    }
    if (!navigation_passable(map, sx, sy) || !navigation_passable(map, gx, gy)) {
        return -3;
    }

    for (uint32_t i = 0; i < total; i++) {
        s_g[i] = NAV_INF;
        s_parent[i] = -1;
        s_closed[i] = 0;
        s_in_open[i] = 0;
    }
    s_open_count = 0;

    s_g[start] = 0;
    s_open[s_open_count++] = start;
    s_in_open[start] = 1;

    while (s_open_count > 0) {
        /* 线性扫描最小 f 节点（栅格规模受 NAVIGATION_MAX_CELLS 约束） */
        uint32_t bi = 0, bf = NAV_INF, bn = start;
        for (uint16_t i = 0; i < s_open_count; i++) {
            uint32_t n = s_open[i];
            uint32_t nx = n % map->width, ny = n / map->width;
            uint32_t f = s_g[n] + nav_h((uint16_t)nx, (uint16_t)ny, gx, gy, use_h);
            if (f < bf) { bf = f; bn = n; bi = i; }
        }
        s_open[bi] = s_open[--s_open_count];
        s_in_open[bn] = 0;

        if (bn == goal) {
            break;
        }
        s_closed[bn] = 1;

        uint16_t cx = (uint16_t)(bn % map->width);
        uint16_t cy = (uint16_t)(bn / map->width);

        for (int d = 0; d < 8; d++) {
            int32_t nx = cx + nav_dirs[d].dx;
            int32_t ny = cy + nav_dirs[d].dy;
            int32_t nidx;
            uint32_t new_g;
            int32_t new_parent;

            if (nx < 0 || ny < 0 || nx >= map->width || ny >= map->height) {
                continue;
            }
            nidx = ny * map->width + nx;
            if (map->cells[nidx] != 0 || s_closed[nidx]) {
                continue;   /* 障碍或已处理 */
            }
            if (use_los) {
                int32_t gp = s_parent[bn];
                if (gp >= 0 && nav_line_of_sight(map, gp, nidx)) {
                    new_parent = gp;
                    new_g = s_g[gp] + nav_octile((uint16_t)(gp % map->width),
                                                 (uint16_t)(gp / map->width),
                                                 (uint16_t)nx, (uint16_t)ny);
                } else {
                    new_parent = (int32_t)bn;
                    new_g = s_g[bn] + nav_dirs[d].cost;
                }
            } else {
                new_parent = (int32_t)bn;
                new_g = s_g[bn] + nav_dirs[d].cost;
            }

            if (new_g < s_g[nidx]) {
                s_g[nidx] = new_g;
                s_parent[nidx] = new_parent;
                if (!s_in_open[nidx]) {
                    s_open[s_open_count++] = (uint32_t)nidx;
                    s_in_open[nidx] = 1;
                }
            }
        }
    }

    if (s_g[goal] == NAV_INF) {
        return -4;   /* no path */
    }
    return nav_build_path(map, (int32_t)start, (int32_t)goal, path);
}

/* ============ 对外算法入口 ============ */

int navigation_dijkstra(const navigation_grid_map_t *map,
                     uint16_t sx, uint16_t sy, uint16_t gx, uint16_t gy,
                     navigation_path_t *path)
{
    return nav_astar_search(map, sx, sy, gx, gy, false, false, path);
}

int navigation_astar(const navigation_grid_map_t *map,
                  uint16_t sx, uint16_t sy, uint16_t gx, uint16_t gy,
                  navigation_path_t *path)
{
    return nav_astar_search(map, sx, sy, gx, gy, true, false, path);
}

int navigation_thetastar(const navigation_grid_map_t *map,
                      uint16_t sx, uint16_t sy, uint16_t gx, uint16_t gy,
                      navigation_path_t *path)
{
    return nav_astar_search(map, sx, sy, gx, gy, true, true, path);
}