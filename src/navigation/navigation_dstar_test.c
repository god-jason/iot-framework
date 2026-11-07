/* robot/navigation D* Lite 单元测试：静态可达/不可达、同点、容量与重规划。 */
#include "test.h"
#include "navigation.h"

static uint8_t s_cells[25];
static uint8_t s_dummy = 0;
static navigation_path_t s_path;

static void open_map(navigation_grid_map_t *m)
{
    memset(s_cells, 0, sizeof(s_cells));
    m->width = 5;
    m->height = 5;
    m->cells = s_cells;
}

void ut_robot_dstar(void)
{
    navigation_grid_map_t m;
    navigation_grid_map_t big;
    uint16_t len;

    tf_begin("robot/dstar 参数校验");
    open_map(&m);
    TF_CHECK_INT(navigation_dstar_lite(NULL, 0, 0, 1, 1, &s_path), -1);
    TF_CHECK_INT(navigation_dstar_lite(&m, 0, 0, 1, 1, NULL), -1);
    m.cells = NULL;
    TF_CHECK_INT(navigation_dstar_lite(&m, 0, 0, 1, 1, &s_path), -1);
    tf_end();

    tf_begin("robot/dstar 未初始化重规划");
    TF_CHECK_INT(navigation_dstar_replan(NULL, &s_path), -1);
    tf_end();

    tf_begin("robot/dstar 起点等于终点");
    open_map(&m);
    TF_CHECK_INT(navigation_dstar_lite(&m, 2, 2, 2, 2, &s_path), 0);
    TF_CHECK_INT(s_path.len, 1);
    TF_CHECK_INT(s_path.nodes[0], navigation_idx(2, 2, 5));
    tf_end();

    tf_begin("robot/dstar 简单栅格可达路径");
    open_map(&m);
    TF_CHECK_INT(navigation_dstar_lite(&m, 0, 0, 4, 4, &s_path), 0);
    TF_CHECK(s_path.len >= 5);
    TF_CHECK_INT(s_path.nodes[0], navigation_idx(0, 0, 5));
    TF_CHECK_INT(s_path.nodes[s_path.len - 1], navigation_idx(4, 4, 5));
    tf_end();

    tf_begin("robot/dstar 障碍墙导致不可达");
    open_map(&m);
    {
        uint16_t y;
        for (y = 0; y < 5; y++) {
            s_cells[y * 5 + 2] = 1;
        }
    }
    TF_CHECK_INT(navigation_dstar_lite(&m, 0, 0, 4, 4, &s_path), -4);
    tf_end();

    tf_begin("robot/dstar 栅格数超限");
    big.width = 65;
    big.height = 64;
    big.cells = &s_dummy;
    TF_CHECK_INT(navigation_dstar_lite(&big, 0, 0, 1, 1, &s_path), -2);
    tf_end();

    tf_begin("robot/dstar 同一地图增量重规划");
    open_map(&m);
    TF_CHECK_INT(navigation_dstar_lite(&m, 0, 0, 4, 4, &s_path), 0);
    len = s_path.len;
    TF_CHECK_INT(navigation_dstar_replan(&m, &s_path), 0);
    TF_CHECK_INT(s_path.len, len);
    TF_CHECK_INT(s_path.nodes[0], navigation_idx(0, 0, 5));
    TF_CHECK_INT(s_path.nodes[s_path.len - 1], navigation_idx(4, 4, 5));

    /* map 传 NULL 时沿用上次地图指针 */
    TF_CHECK_INT(navigation_dstar_replan(NULL, &s_path), 0);
    TF_CHECK_INT(navigation_dstar_replan(&m, NULL), -1);
    tf_end();
}
