/* robot/navigation RRT 单元测试：参数校验、简单可达与封闭目标不可达。
 * 采样使用内部固定种子的 LCG，结果确定；完整随机规划性能不在本用例覆盖范围。 */
#include "test_framework.h"
#include "navigation.h"

static uint8_t s_cells2[2];
static uint8_t s_cells9[9];
static uint8_t s_cells25[25];
static navigation_path_t s_path;

void ut_robot_rrt(void)
{
    navigation_grid_map_t m;

    tf_begin("robot/rrt 参数校验");
    m.width = 5;
    m.height = 5;
    m.cells = s_cells25;
    memset(s_cells25, 0, sizeof(s_cells25));
    TF_CHECK_INT(navigation_rrt(NULL, 0, 0, 1, 1, &s_path), -1);
    TF_CHECK_INT(navigation_rrt(&m, 0, 0, 1, 1, NULL), -1);
    m.cells = NULL;
    TF_CHECK_INT(navigation_rrt(&m, 0, 0, 1, 1, &s_path), -1);
    tf_end();

    tf_begin("robot/rrt 起点或终点为障碍");
    memset(s_cells9, 0, sizeof(s_cells9));
    m.width = 3;
    m.height = 3;
    m.cells = s_cells9;
    s_cells9[0] = 1;
    TF_CHECK_INT(navigation_rrt(&m, 0, 0, 2, 2, &s_path), -3);

    memset(s_cells9, 0, sizeof(s_cells9));
    s_cells9[8] = 1;
    TF_CHECK_INT(navigation_rrt(&m, 0, 0, 2, 2, &s_path), -3);
    tf_end();

    tf_begin("robot/rrt 越界起点");
    memset(s_cells25, 0, sizeof(s_cells25));
    m.width = 5;
    m.height = 5;
    m.cells = s_cells25;
    TF_CHECK_INT(navigation_rrt(&m, 10, 10, 4, 4, &s_path), -3);
    tf_end();

    tf_begin("robot/rrt 相邻目标必达");
    s_cells2[0] = 0;
    s_cells2[1] = 0;
    m.width = 2;
    m.height = 1;
    m.cells = s_cells2;
    TF_CHECK_INT(navigation_rrt(&m, 0, 0, 1, 0, &s_path), 0);
    TF_CHECK_INT(s_path.len, 2);
    TF_CHECK_INT(s_path.nodes[0], navigation_idx(0, 0, 2));
    TF_CHECK_INT(s_path.nodes[1], navigation_idx(1, 0, 2));
    tf_end();

    tf_begin("robot/rrt 空旷地图可达");
    memset(s_cells25, 0, sizeof(s_cells25));
    m.width = 5;
    m.height = 5;
    m.cells = s_cells25;
    TF_CHECK_INT(navigation_rrt(&m, 0, 0, 4, 4, &s_path), 0);
    TF_CHECK(s_path.len >= 2);
    TF_CHECK_INT(s_path.nodes[0], navigation_idx(0, 0, 5));
    TF_CHECK_INT(s_path.nodes[s_path.len - 1], navigation_idx(4, 4, 5));
    tf_end();

    tf_begin("robot/rrt 目标被封闭则不可达");
    memset(s_cells25, 0, sizeof(s_cells25));
    m.width = 5;
    m.height = 5;
    m.cells = s_cells25;
    /* 封死 (4,4) 的三个邻居 */
    s_cells25[3 * 5 + 3] = 1;
    s_cells25[4 * 5 + 3] = 1;
    s_cells25[3 * 5 + 4] = 1;
    TF_CHECK_INT(navigation_rrt(&m, 0, 0, 4, 4, &s_path), -4);
    tf_end();
}
