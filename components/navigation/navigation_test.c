/*
 * navigation_test.c - 路径规划测试（对应 navigation.c）
 *
 * 覆盖：坐标转换、可通行判定、Dijkstra / A* / Theta* 的路径有效性、
 *       不可达场景、RRT 与 D* Lite 的增量重规划。
 */

#include "test.h"
#include "navigation.h"

/* 5x5 地图：中间一列障碍，仅底部留一个缺口 */
static const uint8_t s_cells[25] = {
    0, 0, 1, 0, 0,
    0, 0, 1, 0, 0,
    0, 0, 1, 0, 0,
    0, 0, 1, 0, 0,
    0, 0, 0, 0, 0,
};

/* 完全封闭：中间整列都是障碍 */
static const uint8_t s_closed[25] = {
    0, 0, 1, 0, 0,
    0, 0, 1, 0, 0,
    0, 0, 1, 0, 0,
    0, 0, 1, 0, 0,
    0, 0, 1, 0, 0,
};

static bool path_adjacent(const navigation_path_t *p)
{
    uint16_t i;

    if (p->len < 2U) {
        return true;
    }
    for (i = 1U; i < p->len; i++) {
        int dx = (int)(p->nodes[i] % 5U) - (int)(p->nodes[i - 1U] % 5U);
        int dy = (int)(p->nodes[i] / 5U) - (int)(p->nodes[i - 1U] / 5U);
        if (dx < -1 || dx > 1 || dy < -1 || dy > 1) {
            return false;
        }
        if (dx == 0 && dy == 0) {
            return false;
        }
    }
    return true;
}

void ut_robot_nav(void)
{
    navigation_grid_map_t map;
    navigation_path_t path;

    map.width = 5U;
    map.height = 5U;
    map.cells = s_cells;

    tf_begin("navigation/坐标与可通行");

    TF_CHECK_INT(navigation_idx(2U, 3U, 5U), 17U);
    TF_CHECK(navigation_passable(&map, 0U, 0U));
    TF_CHECK(!navigation_passable(&map, 2U, 0U));
    TF_CHECK(!navigation_passable(&map, 9U, 0U));      /* 越界 */
    TF_CHECK(!navigation_passable(NULL, 0U, 0U));

    tf_end();

    tf_begin("navigation/Dijkstra 与 A*");

    TF_CHECK_INT(navigation_dijkstra(&map, 0U, 0U, 4U, 4U, &path), 0);
    TF_CHECK(path.len > 0U);
    TF_CHECK_INT(path.nodes[0], navigation_idx(0U, 0U, 5U));
    TF_CHECK_INT(path.nodes[path.len - 1U], navigation_idx(4U, 4U, 5U));
    TF_CHECK(path_adjacent(&path));

    TF_CHECK_INT(navigation_astar(&map, 0U, 0U, 4U, 4U, &path), 0);
    TF_CHECK_INT(path.nodes[0], navigation_idx(0U, 0U, 5U));
    TF_CHECK_INT(path.nodes[path.len - 1U], navigation_idx(4U, 4U, 5U));

    TF_CHECK_INT(navigation_thetastar(&map, 0U, 0U, 4U, 4U, &path), 0);
    TF_CHECK_INT(path.nodes[0], navigation_idx(0U, 0U, 5U));
    TF_CHECK_INT(path.nodes[path.len - 1U], navigation_idx(4U, 4U, 5U));

    tf_end();

    tf_begin("navigation/异常与不可达");

    TF_CHECK_INT(navigation_astar(NULL, 0U, 0U, 4U, 4U, &path), -1);
    TF_CHECK_INT(navigation_astar(&map, 0U, 0U, 4U, 4U, NULL), -1);
    TF_CHECK_INT(navigation_astar(&map, 2U, 0U, 4U, 4U, &path), -3);

    {
        navigation_grid_map_t closed;

        closed.width = 5U;
        closed.height = 5U;
        closed.cells = s_closed;
        TF_CHECK_INT(navigation_astar(&closed, 0U, 0U, 4U, 4U, &path), -4);
    }

    tf_end();
}

void ut_robot_dstar(void)
{
    navigation_grid_map_t map;
    navigation_path_t path;
    uint8_t mutable_cells[25];

    memcpy(mutable_cells, s_cells, sizeof(mutable_cells));
    map.width = 5U;
    map.height = 5U;
    map.cells = mutable_cells;

    tf_begin("navigation/D* Lite 首次求解");

    TF_CHECK_INT(navigation_dstar_lite(&map, 0U, 0U, 4U, 4U, &path), 0);
    TF_CHECK_INT(path.nodes[0], navigation_idx(0U, 0U, 5U));
    TF_CHECK_INT(path.nodes[path.len - 1U], navigation_idx(4U, 4U, 5U));

    tf_end();

    tf_begin("navigation/D* Lite 增量重规划");

    /* 在缺口处新增障碍，需绕行或改为不可达 */
    mutable_cells[navigation_idx(2U, 4U, 5U)] = 1;
    TF_CHECK_INT(navigation_dstar_replan(&map, &path), -4);   /* 无路可走 */

    /* 恢复后应重新可达 */
    mutable_cells[navigation_idx(2U, 4U, 5U)] = 0;
    TF_CHECK_INT(navigation_dstar_replan(NULL, &path), 0);
    TF_CHECK_INT(path.nodes[path.len - 1U], navigation_idx(4U, 4U, 5U));

    TF_CHECK_INT(navigation_dstar_replan(&map, NULL), -1);

    tf_end();
}

void ut_robot_rrt(void)
{
    navigation_grid_map_t map;
    navigation_path_t path;

    map.width = 5U;
    map.height = 5U;
    map.cells = s_cells;

    tf_begin("navigation/RRT");

    /* 固定随机种子，结果可复现 */
    TF_CHECK_INT(navigation_rrt(&map, 0U, 0U, 4U, 4U, &path), 0);
    TF_CHECK(path.len >= 2U);
    TF_CHECK_INT(path.nodes[0], navigation_idx(0U, 0U, 5U));
    TF_CHECK_INT(path.nodes[path.len - 1U], navigation_idx(4U, 4U, 5U));

    TF_CHECK_INT(navigation_rrt(NULL, 0U, 0U, 4U, 4U, &path), -1);
    TF_CHECK_INT(navigation_rrt(&map, 0U, 0U, 4U, 4U, NULL), -1);

    tf_end();
}
