/*
 * 导航公共实现：栅格索引/通行判断、A* 核心（供 Dijkstra / A-star / Theta-star 复用）。
 *
 * 算法流程（nav_astar_search）：
 *   1) 校验参数与地图规模，检查起点/终点可通行；
 *   2) 清空 g/parent/closed/open 等运行期工作数组；
 *   3) 从起点出发，每轮线性扫描 open 表取 f 最小节点扩展八邻域；
 *   4) 松弛成功则更新 g 与 parent（Theta* 下可能改用祖父节点）；
 *   5) 到达终点后沿 parent 反向回溯并翻转，得到“起点→终点”路径。
 *
 * 数据结构：
 *   - s_g[]：起点到该格的当前最优代价（NAV_INF 表示不可达）；
 *   - s_parent[]：最优路径上的父节点索引（-1 表示无）；
 *   - s_closed[]：已扩展标记；
 *   - s_open[]/s_in_open[]：开放表（数组 + 成员标记，允许重复入表时去重）。
 *
 * 复杂度与边界：
 *   - open 表采用线性扫描找最小值，O(V^2) 级别，但受 NAVIGATION_MAX_CELLS 限制可接受；
 *   - 地图总格数超过 NAVIGATION_MAX_CELLS 直接返回 -2，不做任何搜索；
 *   - 代价采用 10/14 整数近似，避免浮点运算。
 */
#include "navigation.h"
#include "iot_mem.h"

#include <string.h>

/* ============ 基础工具 ============ */

/*
 * 栅格坐标转线性索引。
 * @return y * width + x（行优先）
 */
uint32_t navigation_idx(uint16_t x, uint16_t y, uint16_t width)
{
    return (uint32_t)y * width + x;
}

/*
 * 判断栅格是否可通行。
 * @return true 表示坐标在界内且 cells 值为 0；参数非法或越界均返回 false
 */
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

/* 整数绝对值 */
static int32_t nav_abs(int32_t v) { return (v < 0) ? -v : v; }

/* octile 距离：八向移动下的最小代价估计（对角 14 + 直行 10） */
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

/* Bresenham 视线判断：a 到 b 之间所有栅格均可通行（不含起点 a，含终点 b） */
static bool nav_line_of_sight(const navigation_grid_map_t *map, int32_t a, int32_t b)
{
    int32_t w = map->width;
    int32_t x0 = a % w, y0 = a / w;     /* 线性索引还原为坐标 */
    int32_t x1 = b % w, y1 = b / w;
    int32_t dx = nav_abs(x1 - x0), dy = nav_abs(y1 - y0);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx - dy;
    int32_t x = x0, y = y0;

    while (x != x1 || y != y1) {
        int32_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }   /* 沿 x 方向推进 */
        if (e2 < dx)  { err += dx; y += sy; }   /* 沿 y 方向推进 */
        if (x == x1 && y == y1) {
            break;                              /* 终点由调用方保证可通行，不再检查 */
        }
        if (!navigation_passable(map, (uint16_t)x, (uint16_t)y)) {
            return false;                       /* 途中遇到障碍：视线被遮挡 */
        }
    }
    return true;
}

/* ============ A* 核心（Dijkstra / A-star / Theta-star 共用） ============ */

#define NAV_INF 0xFFFFFFFFu     /* 不可达/无穷大代价 */

/* 工作数组：按栅格总数索引，容量受 NAVIGATION_MAX_CELLS 约束，首次搜索时惰性分配 */
static uint32_t *s_g = NULL;        /* 起点到各格的最优代价 */
static int32_t  *s_parent = NULL;   /* 最优路径父节点 */
static uint8_t  *s_closed = NULL;   /* 已扩展标记 */
static uint32_t *s_open = NULL;     /* 开放表（存栅格索引） */
static uint8_t  *s_in_open = NULL;  /* 是否已在开放表中 */
static uint16_t s_open_count;       /* 开放表当前元素数 */

/*
 * 惰性分配工作数组；已分配则直接复用，不重复分配。
 * @return 0 成功（已分配或本次分配成功）；-1 分配失败
 */
static int nav_ensure(void)
{
    if (s_g != NULL && s_parent != NULL && s_closed != NULL &&
        s_open != NULL && s_in_open != NULL) {
        return 0;
    }
    if (s_g == NULL) {
        s_g = (uint32_t *)iot_mem_calloc(NAVIGATION_MAX_CELLS, sizeof(*s_g));
    }
    if (s_parent == NULL) {
        s_parent = (int32_t *)iot_mem_calloc(NAVIGATION_MAX_CELLS, sizeof(*s_parent));
    }
    if (s_closed == NULL) {
        s_closed = (uint8_t *)iot_mem_calloc(NAVIGATION_MAX_CELLS, sizeof(*s_closed));
    }
    if (s_open == NULL) {
        s_open = (uint32_t *)iot_mem_calloc(NAVIGATION_MAX_CELLS, sizeof(*s_open));
    }
    if (s_in_open == NULL) {
        s_in_open = (uint8_t *)iot_mem_calloc(NAVIGATION_MAX_CELLS, sizeof(*s_in_open));
    }
    if (s_g == NULL || s_parent == NULL || s_closed == NULL ||
        s_open == NULL || s_in_open == NULL) {
        iot_mem_free(s_g);
        iot_mem_free(s_parent);
        iot_mem_free(s_closed);
        iot_mem_free(s_open);
        iot_mem_free(s_in_open);
        s_g = NULL;
        s_parent = NULL;
        s_closed = NULL;
        s_open = NULL;
        s_in_open = NULL;
        return -1;                  /* 分配失败：整体释放，便于下次重试 */
    }
    return 0;
}

/* 八邻域方向与移动代价 */
typedef struct { int16_t dx, dy; uint32_t cost; } nav_dir_t;
static const nav_dir_t nav_dirs[8] = {
    { 1, 0, 10}, {-1, 0, 10}, {0, 1, 10}, {0, -1, 10},
    { 1, 1, 14}, { 1,-1, 14}, {-1, 1, 14}, {-1,-1, 14}
};

/*
 * 沿 s_parent 从终点回溯到起点，生成正向路径。
 * @param start/goal 起止栅格索引
 * @param path      输出路径
 * @return 0 成功；-1 回溯未到达起点（parent 链断裂）
 * @note 先用临时数组倒序收集，再翻转写入 path；路径过长时在 NAVIGATION_MAX_NODES 处截断。
 */
static int nav_build_path(const navigation_grid_map_t *map, int32_t start, int32_t goal,
                          navigation_path_t *path)
{
    uint16_t temp[NAVIGATION_MAX_NODES];
    uint16_t len = 0;
    int32_t cur = goal;
    (void)map;

    while (cur >= 0 && len < NAVIGATION_MAX_NODES) {
        temp[len++] = (uint16_t)cur;    /* 从终点向起点倒序收集 */
        if (cur == start) {
            break;
        }
        cur = s_parent[cur];
    }
    if (cur != start) {
        return -1;                      /* 未回溯到起点：无有效路径 */
    }
    for (uint16_t i = 0; i < len; i++) {
        path->nodes[i] = temp[len - 1 - i];     /* 翻转为 起点→终点 */
    }
    path->len = len;
    return 0;
}

/*
 * A* 搜索核心，三种算法通过开关复用同一实现。
 * @param use_h  是否使用启发函数（false → Dijkstra）
 * @param use_los 是否启用视线跳父（true → Theta*）
 * @param path   输出路径
 * @return 0 成功；-1 参数非法；-2 地图超容量或工作数组分配失败；-3 起终点不可通行；-4 无路径
 */
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
        return -2;                      /* 超出工作数组容量 */
    }
    if (!navigation_passable(map, sx, sy) || !navigation_passable(map, gx, gy)) {
        return -3;                      /* 起/终点位于障碍或界外 */
    }
    if (nav_ensure() != 0) {
        return -2;                      /* 工作数组分配失败 */
    }

    /* 初始化工作数组：仅需清 total 个有效格 */
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
        /* 线性扫描最小 f 节点（栅格规模受 NAVIGATION_MAX_CELLS 约束，无需优先队列） */
        uint32_t bi = 0, bf = NAV_INF, bn = start;
        for (uint16_t i = 0; i < s_open_count; i++) {
            uint32_t n = s_open[i];
            uint32_t nx = n % map->width, ny = n / map->width;
            uint32_t f = s_g[n] + nav_h((uint16_t)nx, (uint16_t)ny, gx, gy, use_h);
            if (f < bf) { bf = f; bn = n; bi = i; }     /* 记录最优节点及其在开放表中的位置 */
        }
        s_open[bi] = s_open[--s_open_count];    /* 用末尾元素覆盖被取出的元素，O(1) 删除 */
        s_in_open[bn] = 0;

        if (bn == goal) {
            break;                              /* 已到达终点，可回溯 */
        }
        s_closed[bn] = 1;                       /* 标记为已扩展 */

        uint16_t cx = (uint16_t)(bn % map->width);
        uint16_t cy = (uint16_t)(bn / map->width);

        for (int d = 0; d < 8; d++) {
            int32_t nx = cx + nav_dirs[d].dx;
            int32_t ny = cy + nav_dirs[d].dy;
            int32_t nidx;
            uint32_t new_g;
            int32_t new_parent;

            if (nx < 0 || ny < 0 || nx >= map->width || ny >= map->height) {
                continue;                       /* 越界邻居直接跳过 */
            }
            nidx = ny * map->width + nx;
            if (map->cells[nidx] != 0 || s_closed[nidx]) {
                continue;   /* 障碍或已处理 */
            }
            if (use_los) {
                /* Theta*：若祖父节点到邻居视线通畅，则以祖父为父（路径可跨格斜行） */
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
                /* 松弛成功：更新代价与父节点，必要时加入开放表 */
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
        return -4;   /* 终点代价仍为无穷：无可行路径 */
    }
    return nav_build_path(map, (int32_t)start, (int32_t)goal, path);
}

/* ============ 对外算法入口 ============ */

/*
 * Dijkstra：关闭启发与视线跳父，退化为按 g 值最小扩展的经典最短路。
 * @return 0 成功；负值含义见 nav_astar_search
 */
int navigation_dijkstra(const navigation_grid_map_t *map,
                     uint16_t sx, uint16_t sy, uint16_t gx, uint16_t gy,
                     navigation_path_t *path)
{
    return nav_astar_search(map, sx, sy, gx, gy, false, false, path);
}

/*
 * A*：启用 octile 启发，不做视线跳父。
 * @return 0 成功；负值含义见 nav_astar_search
 */
int navigation_astar(const navigation_grid_map_t *map,
                  uint16_t sx, uint16_t sy, uint16_t gx, uint16_t gy,
                  navigation_path_t *path)
{
    return nav_astar_search(map, sx, sy, gx, gy, true, false, path);
}

/*
 * Theta*：同时启用启发与视线跳父，得到拐点更少的任意角路径。
 * @return 0 成功；负值含义见 nav_astar_search
 */
int navigation_thetastar(const navigation_grid_map_t *map,
                      uint16_t sx, uint16_t sy, uint16_t gx, uint16_t gy,
                      navigation_path_t *path)
{
    return nav_astar_search(map, sx, sy, gx, gy, true, true, path);
}