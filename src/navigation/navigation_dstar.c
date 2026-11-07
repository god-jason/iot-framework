/*
 * D* Lite 动态环境路径规划（反向搜索，支持障碍变化后增量重规划）。
 *
 * 算法要点：
 *   - 从“目标点”向“起点”反向搜索，为每格维护 g（实际代价）与 rhs（一步前瞻代价）；
 *   - 不一致节点（g != rhs）放入优先队列 U，键值为 (min(g,rhs)+h, min(g,rhs))；
 *   - dstar_compute 反复弹出最小键节点并修正其周围节点，直到起点一致；
 *   - 障碍变化后只需把受影响的边重新计算，无需从头搜索（见 navigation_dstar_replan）。
 *
 * 数据结构（工作数组首次调用时惰性分配，容量 NAVIGATION_MAX_CELLS）：
 *   - g[]/rhs[]：代价表；
 *   - u_list[]/u_in[]/u_cnt：优先队列（数组实现，弹出时线性找最小键）；
 *   - s_map/s_start/s_goal：上一次搜索的上下文，供增量重规划复用；
 *   - s_km：D* Lite 中随起点移动累加的启发修正量（本实现起点固定，仅初始化为 0）。
 *
 * 边界与注意事项：
 *   - 地图格数超过 NAVIGATION_MAX_CELLS 返回 -2；
 *   - 队列容量按格数上限分配，push 前依赖节点去重标记 u_in，不会重复入队；
 *   - dstar_compute 的终止条件使用“起点键不小于最小键且起点一致”；
 *   - 全局状态可被下一次 navigation_dstar_lite 覆盖，非线程安全。
 */
#include "navigation.h"
#include "port_mem.h"

#include <string.h>

#define DSTAR_INF 0x3FFFFFFFu    /* 代价无穷大（留出加法余量，避免溢出） */

static const navigation_grid_map_t *s_map;   /* 当前搜索使用的地图（外部持有） */
static uint32_t s_start, s_goal, s_km;       /* 起点/终点线性索引与启发修正量 */

static uint32_t *g = NULL;       /* 各格实际代价（惰性分配） */
static uint32_t *rhs = NULL;     /* 各格一步前瞻代价（惰性分配） */
static uint32_t *u_list = NULL;  /* 优先队列（存栅格索引，惰性分配） */
static uint8_t  *u_in = NULL;    /* 队列成员标记，用于去重（惰性分配） */
static uint16_t u_cnt;           /* 队列元素个数 */

/*
 * 惰性分配代价表与优先队列；已分配则直接复用，不重复分配。
 * @return 0 成功（已分配或本次分配成功）；-1 分配失败
 */
static int dstar_ensure(void)
{
    if (g != NULL && rhs != NULL && u_list != NULL && u_in != NULL) {
        return 0;
    }
    if (g == NULL) {
        g = (uint32_t *)port_calloc(NAVIGATION_MAX_CELLS, sizeof(*g));
    }
    if (rhs == NULL) {
        rhs = (uint32_t *)port_calloc(NAVIGATION_MAX_CELLS, sizeof(*rhs));
    }
    if (u_list == NULL) {
        u_list = (uint32_t *)port_calloc(NAVIGATION_MAX_CELLS, sizeof(*u_list));
    }
    if (u_in == NULL) {
        u_in = (uint8_t *)port_calloc(NAVIGATION_MAX_CELLS, sizeof(*u_in));
    }
    if (g == NULL || rhs == NULL || u_list == NULL || u_in == NULL) {
        port_free(g);
        port_free(rhs);
        port_free(u_list);
        port_free(u_in);
        g = NULL;
        rhs = NULL;
        u_list = NULL;
        u_in = NULL;
        return -1;                  /* 分配失败：整体释放，便于下次重试 */
    }
    return 0;
}

/* 八邻域偏移 */
static const int8_t D8X[8] = { 1,-1, 0, 0, 1, 1,-1,-1 };
static const int8_t D8Y[8] = { 0, 0, 1,-1, 1,-1, 1,-1 };

/* 整数绝对值 */
static int32_t dabs(int32_t v) { return (v < 0) ? -v : v; }

/* 两个线性索引之间的 octile 距离（对角 14 + 直行 10，与 A* 保持一致） */
static uint32_t dstar_octile(uint32_t a, uint32_t b)
{
    int32_t w = s_map->width;
    int32_t ax = a % w, ay = a / w;
    int32_t bx = b % w, by = b / w;
    int32_t dx = dabs(ax - bx), dy = dabs(ay - by);
    if (dx > dy) {
        return (uint32_t)(14 * dy + 10 * (dx - dy));
    }
    return (uint32_t)(14 * dx + 10 * (dy - dx));
}

/* 启发值：节点到起点的 octile 距离（反向搜索以起点为启发基准） */
static uint32_t dstar_h(uint32_t s) { return dstar_octile(s, s_start); }

/*
 * 计算节点的优先队列键值 (k1, k2)。
 * k1 = min(g,rhs) + h，k2 = min(g,rhs)：先比 k1，再比 k2。
 */
static void dstar_keys(uint32_t s, uint32_t *k1, uint32_t *k2)
{
    uint32_t m = (g[s] < rhs[s]) ? g[s] : rhs[s];
    *k1 = m + dstar_h(s);
    *k2 = m;
}

/* 比较两个节点的键值是否 a < b（按 k1、k2 字典序） */
static int dstar_key_less(uint32_t a, uint32_t b)
{
    uint32_t a1, a2, b1, b2;
    dstar_keys(a, &a1, &a2);
    dstar_keys(b, &b1, &b2);
    if (a1 != b1) {
        return a1 < b1;
    }
    return a2 < b2;
}

/* 入队：节点已在队列中则忽略（u_in 去重，保证同一节点不重复占用队列空间） */
static void dstar_u_push(uint32_t s)
{
    if (!u_in[s]) {
        u_list[u_cnt++] = s;
        u_in[s] = 1;
    }
}

/*
 * 出队：线性扫描 key 最小的节点并移除。
 * @return 弹出的节点线性索引；队列为空时行为未定义（调用方需保证 u_cnt>0）
 * @note 删除采用“末尾元素覆盖”策略，不保持数组顺序。
 */
static uint32_t dstar_u_pop(void)
{
    uint16_t bi = 0;
    for (uint16_t i = 1; i < u_cnt; i++) {
        if (dstar_key_less(u_list[i], u_list[bi])) {
            bi = i;
        }
    }
    uint32_t r = u_list[bi];
    u_list[bi] = u_list[--u_cnt];
    u_in[r] = 0;
    return r;
}

/* 从队列中移除指定节点（若存在），并清除去重标记 */
static void dstar_u_remove(uint32_t s)
{
    for (uint16_t i = 0; i < u_cnt; i++) {
        if (u_list[i] == s) {
            u_list[i] = u_list[--u_cnt];
            break;
        }
    }
    u_in[s] = 0;
}

/*
 * 更新节点 u 的 rhs 值，并按一致性决定是否重新入队。
 * - 目标点的 rhs 恒为 0，不重算；
 * - 其余节点取所有可通行邻居 (代价 + g) 的极小值，无邻居则为 DSTAR_INF；
 * - 若 u 当前在队列中先移除，再根据 g != rhs 决定是否重新入队。
 * @param u 待更新的节点线性索引
 */
static void dstar_update_vertex(uint32_t u)
{
    if (u != s_goal) {
        uint32_t best = DSTAR_INF;
        uint16_t cx = u % s_map->width, cy = u / s_map->width;
        for (int i = 0; i < 8; i++) {
            int32_t nx = cx + D8X[i], ny = cy + D8Y[i];
            if (nx < 0 || ny < 0 || nx >= s_map->width || ny >= s_map->height) {
                continue;
            }
            uint32_t n = ny * s_map->width + nx;
            if (s_map->cells[n] != 0) {
                continue;
            }
            uint32_t c = dstar_octile(u, n) + g[n];
            if (c < best) {
                best = c;
            }
        }
        rhs[u] = best;
    }
    if (u_in[u]) {
        dstar_u_remove(u);
    }
    if (g[u] != rhs[u]) {
        dstar_u_push(u);
    }
}

/*
 * D* Lite 主循环：不断处理不一致节点，直到起点一致且其键值不小于队列最小键。
 * 结束条件即“起点已获得正确代价”，此时可沿 g 递减方向提取路径。
 * 注意：本实现假设搜索过程中起点不变，因此未把 s_km 累加进键值。
 */
static void dstar_compute(void)
{
    while (u_cnt > 0) {
        uint32_t u = u_list[0];
        /* 取最小 key */
        for (uint16_t i = 1; i < u_cnt; i++) {
            if (dstar_key_less(u_list[i], u)) {
                u = u_list[i];
            }
        }
        uint32_t k1, k2, sk1, sk2;
        dstar_keys(u, &k1, &k2);
        dstar_keys(s_start, &sk1, &sk2);
        /* 起点键已不小于最小键，且起点已一致：搜索可终止 */
        if (!dstar_key_less(u, s_start) && rhs[s_start] == g[s_start]) {
            break;
        }

        u = dstar_u_pop();
        if (g[u] > rhs[u]) {
            /* 情况一：节点被过度估计，直接下调 g 并传播到邻居 */
            g[u] = rhs[u];
            uint16_t cx = u % s_map->width, cy = u / s_map->width;
            for (int i = 0; i < 8; i++) {
                int32_t nx = cx + D8X[i], ny = cy + D8Y[i];
                if (nx >= 0 && ny >= 0 && nx < s_map->width && ny < s_map->height &&
                    s_map->cells[ny * s_map->width + nx] == 0) {
                    dstar_update_vertex((uint32_t)(ny * s_map->width + nx));
                }
            }
        } else {
            /* 情况二：节点被低估（或障碍新增），抬升 g 到无穷后重新计算自身与邻居 */
            g[u] = DSTAR_INF;
            dstar_update_vertex(u);
            uint16_t cx = u % s_map->width, cy = u / s_map->width;
            for (int i = 0; i < 8; i++) {
                int32_t nx = cx + D8X[i], ny = cy + D8Y[i];
                if (nx >= 0 && ny >= 0 && nx < s_map->width && ny < s_map->height &&
                    s_map->cells[ny * s_map->width + nx] == 0) {
                    dstar_update_vertex((uint32_t)(ny * s_map->width + nx));
                }
            }
        }
    }
}

/*
 * 提取路径：从 start 沿 g+cost 最小递减走到 goal。
 * @param path 输出路径（写入起点在内的节点序列）
 * @return 0 成功；-4 中途无更优邻居（路径不可达）或超出节点上限
 * @note 若到达节点数上限仍未到 goal，同样返回 -4。
 */
static int dstar_extract(navigation_path_t *path)
{
    uint32_t cur = s_start;
    uint16_t len = 0;

    path->nodes[len++] = (uint16_t)cur;     /* 先写入起点 */
    while (cur != s_goal && len < NAVIGATION_MAX_NODES) {
        uint16_t cx = cur % s_map->width, cy = cur / s_map->width;
        uint32_t best = DSTAR_INF, bestn = cur;
        for (int i = 0; i < 8; i++) {
            int32_t nx = cx + D8X[i], ny = cy + D8Y[i];
            if (nx < 0 || ny < 0 || nx >= s_map->width || ny >= s_map->height) {
                continue;                   /* 越界邻居跳过 */
            }
            uint32_t n = ny * s_map->width + nx;
            if (s_map->cells[n] != 0) {
                continue;                   /* 障碍邻居跳过 */
            }
            /* 选择“移动代价 + 后继 g”最小的邻居，即朝目标方向前进 */
            uint32_t c = dstar_octile(cur, n) + g[n];
            if (c < best) {
                best = c;
                bestn = n;
            }
        }
        if (bestn == cur) {
            return -4;   /* 无路径 */
        }
        cur = bestn;
        path->nodes[len++] = (uint16_t)cur;
    }
    path->len = len;
    return (cur == s_goal) ? 0 : -4;    /* 节点用尽仍未到终点也视为失败 */
}

/*
 * 建立 D* Lite 搜索状态并求解一次。
 * @param map  地图视图，不可为 NULL
 * @param sx,sy 起点栅格；gx,gy 终点栅格
 * @param path 输出路径
 * @return 0 成功；-1 参数非法；-2 地图超容量或工作数组分配失败；-4 无路径
 * @note 会重置 g/rhs/队列，覆盖上一次的增量搜索状态。
 */
int navigation_dstar_lite(const navigation_grid_map_t *map,
                       uint16_t sx, uint16_t sy, uint16_t gx, uint16_t gy,
                       navigation_path_t *path)
{
    uint32_t total;

    if (map == NULL || path == NULL || map->cells == NULL) {
        return -1;
    }
    s_map = map;
    s_start = navigation_idx(sx, sy, map->width);
    s_goal = navigation_idx(gx, gy, map->width);
    s_km = 0;                           /* 起点固定，不累加启发修正量 */
    total = (uint32_t)map->width * map->height;
    if (total > NAVIGATION_MAX_CELLS) {
        return -2;                      /* 超出工作数组容量 */
    }
    if (dstar_ensure() != 0) {
        return -2;                      /* 工作数组分配失败 */
    }

    /* 初始化代价表：全部不可达，队列清空 */
    for (uint32_t i = 0; i < total; i++) {
        g[i] = DSTAR_INF;
        rhs[i] = DSTAR_INF;
        u_in[i] = 0;
    }
    u_cnt = 0;

    rhs[s_goal] = 0;                    /* 目标点 rhs 为 0，作为反向搜索的种子 */
    dstar_u_push(s_goal);
    dstar_compute();
    return dstar_extract(path);
}

/*
 * 障碍变化后增量重规划（外部已修改 map->cells 对应栅格）。
 * @param map  新的地图视图（与首次调用结构一致）；传 NULL 表示沿用上次 s_map
 * @param path 输出新的路径
 * @return 0 成功；-1 参数非法或尚未建立搜索状态；-4 无可行路径
 * @note 依赖 dstar_update_vertex 中“障碍邻居不参与 rhs 计算”来感知新增障碍；
 *       已删除的障碍需由外部重新执行 navigation_dstar_lite 才能正确恢复。
 */
int navigation_dstar_replan(const navigation_grid_map_t *map, navigation_path_t *path)
{
    if (map != NULL) {
        s_map = map;
    }
    if (s_map == NULL || path == NULL ||
        g == NULL || rhs == NULL || u_list == NULL || u_in == NULL) {
        return -1;                      /* 参数非法或尚未成功建立搜索状态 */
    }
    dstar_compute();                    /* 复用已有 g/rhs 与队列做增量修复 */
    return dstar_extract(path);
}