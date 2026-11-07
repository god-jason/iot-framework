/* robot/navigation 公共算法单元测试：索引、通行判断、Dijkstra/A-star/Theta-star。 */
#include "test.h"
#include "navigation.h"

static uint8_t s_cells[25];
static uint8_t s_dummy = 0;
static navigation_path_t s_path;

static void setup_map(navigation_grid_map_t *m, uint8_t *cells)
{
    memset(cells, 0, 25);
    m->width = 5;
    m->height = 5;
    m->cells = cells;
}

static void check_endpoints(uint16_t start_idx, uint16_t goal_idx)
{
    TF_CHECK(s_path.len >= 2);
    TF_CHECK_INT(s_path.nodes[0], start_idx);
    TF_CHECK_INT(s_path.nodes[s_path.len - 1], goal_idx);
}

void ut_robot_nav(void)
{
    navigation_grid_map_t m;
    navigation_grid_map_t big;
    uint8_t cells_b[25];

    tf_begin("robot/nav 索引与通行判断");
    TF_CHECK_INT(navigation_idx(2, 3, 10), 32);
    TF_CHECK_INT(navigation_idx(0, 0, 5), 0);
    TF_CHECK_INT(navigation_idx(4, 4, 5), 24);

    setup_map(&m, s_cells);
    TF_CHECK(navigation_passable(&m, 0, 0) == true);
    s_cells[1] = 1;
    TF_CHECK(navigation_passable(&m, 1, 0) == false);
    TF_CHECK(navigation_passable(&m, 5, 0) == false);
    TF_CHECK(navigation_passable(&m, 0, 5) == false);
    TF_CHECK(navigation_passable(NULL, 0, 0) == false);
    m.cells = NULL;
    TF_CHECK(navigation_passable(&m, 0, 0) == false);
    tf_end();

    tf_begin("robot/nav 参数校验");
    setup_map(&m, s_cells);
    TF_CHECK_INT(navigation_dijkstra(NULL, 0, 0, 1, 1, &s_path), -1);
    TF_CHECK_INT(navigation_astar(&m, 0, 0, 1, 1, NULL), -1);
    m.cells = NULL;
    TF_CHECK_INT(navigation_thetastar(&m, 0, 0, 1, 1, &s_path), -1);
    tf_end();

    tf_begin("robot/nav 起点或终点为障碍");
    setup_map(&m, s_cells);
    s_cells[0] = 1;
    TF_CHECK_INT(navigation_dijkstra(&m, 0, 0, 4, 4, &s_path), -3);

    setup_map(&m, s_cells);
    s_cells[24] = 1;
    TF_CHECK_INT(navigation_astar(&m, 0, 0, 4, 4, &s_path), -3);
    tf_end();

    tf_begin("robot/nav 越界起点");
    setup_map(&m, s_cells);
    TF_CHECK_INT(navigation_astar(&m, 5, 0, 4, 4, &s_path), -3);
    TF_CHECK_INT(navigation_astar(&m, 0, 0, 0, 5, &s_path), -3);
    tf_end();

    tf_begin("robot/nav 栅格数超限");
    big.width = 65;
    big.height = 64;
    big.cells = &s_dummy;
    TF_CHECK_INT(navigation_dijkstra(&big, 0, 0, 1, 1, &s_path), -2);
    TF_CHECK_INT(navigation_astar(&big, 0, 0, 1, 1, &s_path), -2);
    TF_CHECK_INT(navigation_thetastar(&big, 0, 0, 1, 1, &s_path), -2);
    tf_end();

    tf_begin("robot/nav 起点等于终点");
    setup_map(&m, s_cells);
    TF_CHECK_INT(navigation_dijkstra(&m, 2, 2, 2, 2, &s_path), 0);
    TF_CHECK_INT(s_path.len, 1);
    TF_CHECK_INT(s_path.nodes[0], 12);

    TF_CHECK_INT(navigation_astar(&m, 2, 2, 2, 2, &s_path), 0);
    TF_CHECK_INT(s_path.len, 1);
    TF_CHECK_INT(s_path.nodes[0], 12);

    TF_CHECK_INT(navigation_thetastar(&m, 2, 2, 2, 2, &s_path), 0);
    TF_CHECK_INT(s_path.len, 1);
    TF_CHECK_INT(s_path.nodes[0], 12);
    tf_end();

    tf_begin("robot/nav 空旷地图可达");
    setup_map(&m, s_cells);
    TF_CHECK_INT(navigation_dijkstra(&m, 0, 0, 4, 4, &s_path), 0);
    check_endpoints(0, 24);
    TF_CHECK_INT(s_path.len, 5);

    setup_map(&m, s_cells);
    TF_CHECK_INT(navigation_astar(&m, 0, 0, 4, 4, &s_path), 0);
    check_endpoints(0, 24);

    setup_map(&m, s_cells);
    TF_CHECK_INT(navigation_thetastar(&m, 0, 0, 4, 4, &s_path), 0);
    check_endpoints(0, 24);
    tf_end();

    tf_begin("robot/nav 障碍墙不可达");
    setup_map(&m, cells_b);
    {
        uint16_t y;
        for (y = 0; y < 5; y++) {
            cells_b[y * 5 + 2] = 1;
        }
    }
    TF_CHECK_INT(navigation_dijkstra(&m, 0, 0, 4, 4, &s_path), -4);
    TF_CHECK_INT(navigation_astar(&m, 0, 0, 4, 4, &s_path), -4);
    TF_CHECK_INT(navigation_thetastar(&m, 0, 0, 4, 4, &s_path), -4);
    tf_end();
}
