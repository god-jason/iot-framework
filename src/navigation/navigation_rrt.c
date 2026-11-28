/*
 * RRT 快速探索随机树（栅格版）：随机采样 + 朝采样点逐步扩展。
 *
 * 算法流程：
 *   1) 以起点为树根，初始化静态树表；
 *   2) 迭代最多 4000 次：以 10% 概率直接采样目标点（引导性采样），否则全图随机采样；
 *   3) 找到树中距离采样点最近的节点，朝采样点方向移动一格（八邻域）；
 *   4) 新格合法且可通行则加入树，若正好是目标点则回溯父链得到路径。
 *
 * 数据结构：
 *   - s_tree：树的节点表（变长数组，初始容量 NAVIGATION_MAX_NODES），节点 0 恒为起点（parent = -1）；
 *   - rrt_seed：LCG 伪随机种子，固定初值使结果可复现。
 *
 * 边界与注意事项：
 *   - 固定迭代次数，找不到路径返回 -4；树满或工作内存分配失败返回 -5；
 *   - 每次扩展只前进一格，因此路径由大量相邻栅格组成（未做剪枝/平滑）；
 *   - 全局静态状态，不可重入。
 */
#include "navigation.h"
#include "ros.h"
#include "common.h"

#include <string.h>

/* RRT 树节点：栅格坐标 + 父节点在数组中的下标（-1 表示根） */
typedef struct {
    uint16_t x, y;
    int32_t parent;
} rrt_node_t;

static vec_t s_tree;                              /* 树表（变长数组，节点数为 vec_size） */

/*
 * 惰性初始化树表；已初始化则直接复用。
 * @return 0 成功（已初始化或本次初始化成功）；-1 分配失败
 */
static int rrt_ensure(void)
{
    if (vec_data(&s_tree) != NULL) {
        return 0;
    }
    return vec_init(&s_tree, sizeof(rrt_node_t), NAVIGATION_MAX_NODES);
}

/* 简单 LCG 伪随机（自包含，测试用） */
static uint32_t rrt_seed = 12345;
static uint32_t rrt_rand(void)
{
    rrt_seed = rrt_seed * 1103515245u + 12345u;
    return (rrt_seed >> 16) & 0x7FFFu;
}

/*
 * 在树中查找距离 (tx,ty) 最近的节点。
 * @return 最近节点的下标；树非空时必然 >= 0（初始 best=0）
 * @note 使用平方欧氏距离比较，避免开方；map 参数当前未使用。
 */
static int32_t rrt_nearest(const navigation_grid_map_t *map, uint16_t tx, uint16_t ty)
{
    int32_t best = 0;
    uint32_t best_d = 0xFFFFFFFFu;
    (void)map;

    for (uint32_t i = 0; i < vec_size(&s_tree); i++) {
        const rrt_node_t *n = (const rrt_node_t *)vec_at(&s_tree, i);
        uint32_t dx = (n->x > tx) ? (n->x - tx) : (tx - n->x);
        uint32_t dy = (n->y > ty) ? (n->y - ty) : (ty - n->y);
        uint32_t d = dx * dx + dy * dy;
        if (d < best_d) {
            best_d = d;
            best = (int32_t)i;
        }
    }
    return best;
}

/* 符号函数：正数 1、负数 -1、零 0，用于把“朝采样点方向”归一化为单步增量 */
static int nav_sign(int32_t v) { return (v > 0) ? 1 : ((v < 0) ? -1 : 0); }

/*
 * RRT 路径规划入口。
 * @param map 栅格地图；sx,sy 起点；gx,gy 终点；path 输出路径
 * @return 0 成功；-1 参数非法；-3 起终点不可通行；-4 迭代耗尽未找到；-5 树节点满或工作内存分配失败
 * @note 路径顺序为“起点→终点”，节点为线性索引。
 */
int navigation_rrt(const navigation_grid_map_t *map,
                uint16_t sx, uint16_t sy, uint16_t gx, uint16_t gy,
                navigation_path_t *path)
{
    uint16_t w, h;

    if (map == NULL || path == NULL || map->cells == NULL) {
        return -1;
    }
    w = map->width;
    h = map->height;
    if (!navigation_passable(map, sx, sy) || !navigation_passable(map, gx, gy)) {
        return -3;
    }
    if (rrt_ensure() != 0) {
        return -5;                      /* 工作内存分配失败 */
    }

    vec_clear(&s_tree);
    {
        rrt_node_t root;
        root.x = sx;
        root.y = sy;
        root.parent = -1;               /* 起点为根，无父节点 */
        if (vec_push(&s_tree, &root) != 0) {
            return -5;
        }
    }

    for (uint32_t iter = 0; iter < 4000; iter++) {
        uint16_t tx, ty;
        int32_t near;
        const rrt_node_t *near_node;
        uint16_t nx, ny;

        /* 采样：10% 概率直接采样目标 */
        if ((rrt_rand() % 10) == 0) {
            tx = gx; ty = gy;
        } else {
            tx = (uint16_t)(rrt_rand() % w);
            ty = (uint16_t)(rrt_rand() % h);
            if (!navigation_passable(map, tx, ty)) {
                continue;               /* 采样点落在障碍上：丢弃本次采样 */
            }
        }

        near = rrt_nearest(map, tx, ty);
        near_node = (const rrt_node_t *)vec_at(&s_tree, (uint32_t)near);
        /* 朝采样点移动一格（8 向）：各轴增量取符号，得到 8 邻域之一 */
        nx = (uint16_t)((int)near_node->x + nav_sign((int32_t)tx - near_node->x));
        ny = (uint16_t)((int)near_node->y + nav_sign((int32_t)ty - near_node->y));
        if (nx >= w || ny >= h) {
            continue;                   /* 越界：丢弃 */
        }
        if (!navigation_passable(map, nx, ny)) {
            continue;                   /* 新格为障碍：丢弃 */
        }
        if (nx == near_node->x && ny == near_node->y) {
            continue;                   /* 未发生位移（已在采样点上）：丢弃 */
        }

        if (vec_size(&s_tree) >= NAVIGATION_MAX_NODES) {
            return -5;   /* 超出容量 */
        }
        {
            rrt_node_t node;
            node.x = nx;
            node.y = ny;
            node.parent = near;
            if (vec_push(&s_tree, &node) != 0) {
                return -5;
            }
        }

        if (nx == gx && ny == gy) {
            /* 回溯 */
            int32_t c = (int32_t)(vec_size(&s_tree) - 1);
            uint16_t temp[NAVIGATION_MAX_NODES];
            uint16_t len = 0;
            while (c >= 0 && len < NAVIGATION_MAX_NODES) {
                const rrt_node_t *n = (const rrt_node_t *)vec_at(&s_tree, (uint32_t)c);
                temp[len++] = navigation_idx(n->x, n->y, w);  /* 倒序收集 */
                if (c == 0) break;      /* 到达根节点 */
                c = n->parent;
            }
            for (uint16_t i = 0; i < len; i++) {
                path->nodes[i] = temp[len - 1 - i];     /* 翻转为 起点→终点 */
            }
            path->len = len;
            return 0;
        }
    }
    return -4;   /* 未找到路径 */
}