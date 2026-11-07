/* RRT 快速探索随机树（栅格版）：随机采样 + 朝采样点逐步扩展。 */
#include "robot_nav.h"

#include <string.h>

typedef struct {
    uint16_t x, y;
    int32_t parent;
} rrt_node_t;

static rrt_node_t s_tree[ROBOT_NAV_MAX_NODES];
static uint32_t s_tree_count;

/* 简单 LCG 伪随机（自包含，测试用） */
static uint32_t rrt_seed = 12345;
static uint32_t rrt_rand(void)
{
    rrt_seed = rrt_seed * 1103515245u + 12345u;
    return (rrt_seed >> 16) & 0x7FFFu;
}

static int32_t rrt_nearest(const robot_grid_map_t *map, uint16_t tx, uint16_t ty)
{
    int32_t best = 0;
    uint32_t best_d = 0xFFFFFFFFu;
    (void)map;

    for (uint32_t i = 0; i < s_tree_count; i++) {
        uint32_t dx = (s_tree[i].x > tx) ? (s_tree[i].x - tx) : (tx - s_tree[i].x);
        uint32_t dy = (s_tree[i].y > ty) ? (s_tree[i].y - ty) : (ty - s_tree[i].y);
        uint32_t d = dx * dx + dy * dy;
        if (d < best_d) {
            best_d = d;
            best = (int32_t)i;
        }
    }
    return best;
}

static int nav_sign(int32_t v) { return (v > 0) ? 1 : ((v < 0) ? -1 : 0); }

int robot_nav_rrt(const robot_grid_map_t *map,
                uint16_t sx, uint16_t sy, uint16_t gx, uint16_t gy,
                robot_path_t *path)
{
    uint16_t w, h;

    if (map == NULL || path == NULL || map->cells == NULL) {
        return -1;
    }
    w = map->width;
    h = map->height;
    if (!robot_nav_passable(map, sx, sy) || !robot_nav_passable(map, gx, gy)) {
        return -3;
    }

    s_tree_count = 0;
    s_tree[s_tree_count].x = sx;
    s_tree[s_tree_count].y = sy;
    s_tree[s_tree_count].parent = -1;
    s_tree_count++;

    for (uint32_t iter = 0; iter < 4000; iter++) {
        uint16_t tx, ty;
        int32_t near;
        uint16_t nx, ny;

        /* 采样：10% 概率直接采样目标 */
        if ((rrt_rand() % 10) == 0) {
            tx = gx; ty = gy;
        } else {
            tx = (uint16_t)(rrt_rand() % w);
            ty = (uint16_t)(rrt_rand() % h);
            if (!robot_nav_passable(map, tx, ty)) {
                continue;
            }
        }

        near = rrt_nearest(map, tx, ty);
        /* 朝采样点移动一格（8 向） */
        nx = (uint16_t)((int)s_tree[near].x + nav_sign((int32_t)tx - s_tree[near].x));
        ny = (uint16_t)((int)s_tree[near].y + nav_sign((int32_t)ty - s_tree[near].y));
        if (nx >= w || ny >= h) {
            continue;
        }
        if (!robot_nav_passable(map, nx, ny)) {
            continue;
        }
        if (nx == s_tree[near].x && ny == s_tree[near].y) {
            continue;
        }

        if (s_tree_count >= ROBOT_NAV_MAX_NODES) {
            return -5;   /* 超出容量 */
        }
        s_tree[s_tree_count].x = nx;
        s_tree[s_tree_count].y = ny;
        s_tree[s_tree_count].parent = near;
        uint32_t ni = s_tree_count++;
        (void)ni;

        if (nx == gx && ny == gy) {
            /* 回溯 */
            int32_t c = (int32_t)(s_tree_count - 1);
            uint16_t temp[ROBOT_NAV_MAX_NODES];
            uint16_t len = 0;
            while (c >= 0 && len < ROBOT_NAV_MAX_NODES) {
                temp[len++] = robot_nav_idx(s_tree[c].x, s_tree[c].y, w);
                if (c == 0) break;
                c = s_tree[c].parent;
            }
            for (uint16_t i = 0; i < len; i++) {
                path->nodes[i] = temp[len - 1 - i];
            }
            path->len = len;
            return 0;
        }
    }
    return -4;   /* 未找到路径 */
}