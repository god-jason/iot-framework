/* D* Lite 动态环境路径规划（反向搜索，支持障碍变化后增量重规划）。 */
#include "robot_nav.h"

#include <string.h>

#define DSTAR_INF 0x3FFFFFFFu

static const robot_grid_map_t *s_map;
static uint32_t s_start, s_goal, s_km;

static uint32_t g[ROBOT_NAV_MAX_CELLS];
static uint32_t rhs[ROBOT_NAV_MAX_CELLS];
static uint32_t u_list[ROBOT_NAV_MAX_CELLS];
static uint8_t  u_in[ROBOT_NAV_MAX_CELLS];
static uint16_t u_cnt;

static const int8_t D8X[8] = { 1,-1, 0, 0, 1, 1,-1,-1 };
static const int8_t D8Y[8] = { 0, 0, 1,-1, 1,-1, 1,-1 };

static int32_t dabs(int32_t v) { return (v < 0) ? -v : v; }

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

static uint32_t dstar_h(uint32_t s) { return dstar_octile(s, s_start); }

static void dstar_keys(uint32_t s, uint32_t *k1, uint32_t *k2)
{
    uint32_t m = (g[s] < rhs[s]) ? g[s] : rhs[s];
    *k1 = m + dstar_h(s);
    *k2 = m;
}

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

static void dstar_u_push(uint32_t s)
{
    if (!u_in[s]) {
        u_list[u_cnt++] = s;
        u_in[s] = 1;
    }
}

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
        if (!dstar_key_less(u, s_start) && rhs[s_start] == g[s_start]) {
            break;
        }

        u = dstar_u_pop();
        if (g[u] > rhs[u]) {
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

/* 提取路径：从 start 沿 g+cost 最小递减走到 goal */
static int dstar_extract(robot_path_t *path)
{
    uint32_t cur = s_start;
    uint16_t len = 0;

    path->nodes[len++] = (uint16_t)cur;
    while (cur != s_goal && len < ROBOT_NAV_MAX_NODES) {
        uint16_t cx = cur % s_map->width, cy = cur / s_map->width;
        uint32_t best = DSTAR_INF, bestn = cur;
        for (int i = 0; i < 8; i++) {
            int32_t nx = cx + D8X[i], ny = cy + D8Y[i];
            if (nx < 0 || ny < 0 || nx >= s_map->width || ny >= s_map->height) {
                continue;
            }
            uint32_t n = ny * s_map->width + nx;
            if (s_map->cells[n] != 0) {
                continue;
            }
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
    return (cur == s_goal) ? 0 : -4;
}

int robot_nav_dstar_lite(const robot_grid_map_t *map,
                       uint16_t sx, uint16_t sy, uint16_t gx, uint16_t gy,
                       robot_path_t *path)
{
    uint32_t total;

    if (map == NULL || path == NULL || map->cells == NULL) {
        return -1;
    }
    s_map = map;
    s_start = robot_nav_idx(sx, sy, map->width);
    s_goal = robot_nav_idx(gx, gy, map->width);
    s_km = 0;
    total = (uint32_t)map->width * map->height;
    if (total > ROBOT_NAV_MAX_CELLS) {
        return -2;
    }

    for (uint32_t i = 0; i < total; i++) {
        g[i] = DSTAR_INF;
        rhs[i] = DSTAR_INF;
        u_in[i] = 0;
    }
    u_cnt = 0;

    rhs[s_goal] = 0;
    dstar_u_push(s_goal);
    dstar_compute();
    return dstar_extract(path);
}

/* 障碍变化后增量重规划（外部已修改 map->cells 对应栅格） */
int robot_nav_dstar_replan(const robot_grid_map_t *map, robot_path_t *path)
{
    if (map != NULL) {
        s_map = map;
    }
    if (s_map == NULL || path == NULL) {
        return -1;
    }
    dstar_compute();
    return dstar_extract(path);
}