/* robot/mapping 单元测试：占据栅格初始化、坐标换算、射线更新与二值导出。 */
#include "test_framework.h"
#include "mapping.h"

static int16_t s_cells[25];
static uint8_t s_out[25];

static void fill_map(mapping_og_map_t *m, int16_t *cells, uint16_t w, uint16_t h)
{
    memset(cells, 0, (size_t)w * h * sizeof(int16_t));
    mapping_og_map_init(m, w, h, 1.0f, 0.0f, 0.0f, cells);
}

void ut_robot_og_map(void)
{
    mapping_og_map_t m;
    mapping_pose_t pose;
    mapping_scan_point_t pt;
    int16_t cx;
    int16_t cy;

    tf_begin("robot/og_map 初始化与参数校验");
    memset(&m, 0, sizeof(m));
    TF_CHECK_INT(mapping_og_map_init(&m, 5, 5, 1.0f, 0.0f, 0.0f, s_cells), 0);
    TF_CHECK_INT(m.width, 5);
    TF_CHECK_INT(m.height, 5);
    TF_CHECK_NEAR(m.resolution, 1.0f, 1e-6);
    TF_CHECK(m.cells == s_cells);
    TF_CHECK_INT(s_cells[0], 0);
    TF_CHECK_INT(s_cells[24], 0);

    TF_CHECK_INT(mapping_og_map_init(NULL, 5, 5, 1.0f, 0.0f, 0.0f, s_cells), -1);
    TF_CHECK_INT(mapping_og_map_init(&m, 5, 5, 1.0f, 0.0f, 0.0f, NULL), -1);
    TF_CHECK_INT(mapping_og_map_init(&m, 0, 5, 1.0f, 0.0f, 0.0f, s_cells), -1);
    TF_CHECK_INT(mapping_og_map_init(&m, 5, 0, 1.0f, 0.0f, 0.0f, s_cells), -1);
    TF_CHECK_INT(mapping_og_map_init(&m, 5, 5, 0.0f, 0.0f, 0.0f, s_cells), -1);
    TF_CHECK_INT(mapping_og_map_init(&m, 5, 5, -1.0f, 0.0f, 0.0f, s_cells), -1);
    tf_end();

    tf_begin("robot/og_map 世界坐标转栅格");
    fill_map(&m, s_cells, 5, 5);
    TF_CHECK_INT(mapping_world_to_cell(&m, 0.5f, 0.5f, &cx, &cy), 0);
    TF_CHECK_INT(cx, 0);
    TF_CHECK_INT(cy, 0);
    TF_CHECK_INT(mapping_world_to_cell(&m, 3.9f, 3.9f, &cx, &cy), 0);
    TF_CHECK_INT(cx, 3);
    TF_CHECK_INT(cy, 3);
    TF_CHECK_INT(mapping_world_to_cell(&m, 5.0f, 0.0f, &cx, &cy), -1);
    TF_CHECK_INT(mapping_world_to_cell(&m, 0.0f, 5.0f, &cx, &cy), -1);
    TF_CHECK_INT(mapping_world_to_cell(&m, -1.5f, 0.0f, &cx, &cy), -1);
    TF_CHECK_INT(mapping_world_to_cell(NULL, 0.5f, 0.5f, &cx, &cy), -1);
    tf_end();

    tf_begin("robot/og_map 概率读取与越界");
    fill_map(&m, s_cells, 5, 5);
    TF_CHECK_NEAR(mapping_og_map_probability(&m, 0, 0), 0.5f, 1e-5);
    TF_CHECK_NEAR(mapping_og_map_probability(&m, 4, 4), 0.5f, 1e-5);
    TF_CHECK_NEAR(mapping_og_map_probability(&m, -1, 0), 0.5f, 1e-5);
    TF_CHECK_NEAR(mapping_og_map_probability(&m, 5, 0), 0.5f, 1e-5);
    TF_CHECK_NEAR(mapping_og_map_probability(NULL, 0, 0), 0.5f, 1e-5);
    tf_end();

    tf_begin("robot/og_map 命中更新为占据");
    fill_map(&m, s_cells, 5, 5);
    pose.x = 1.5f;
    pose.y = 1.5f;
    pose.theta = 0.0f;
    pt.angle = 0.0f;
    pt.range = 0.5f;
    TF_CHECK_INT(mapping_og_map_update(&m, &pose, &pt, 1, 5.0f), 0);
    /* 终点 (2.0,1.5) -> 栅格 (2,1)，log-odds +85 */
    TF_CHECK_INT(s_cells[1 * 5 + 2], 85);
    TF_CHECK_NEAR(mapping_og_map_probability(&m, 2, 1), 0.7006f, 1e-3);
    TF_CHECK_NEAR(mapping_og_map_probability(&m, 1, 1), 0.5f, 1e-5);
    tf_end();

    tf_begin("robot/og_map 未命中/超量程标为空闲");
    fill_map(&m, s_cells, 5, 5);
    pose.x = 1.5f;
    pose.y = 1.5f;
    pose.theta = 0.0f;
    pt.angle = 0.0f;
    pt.range = 0.0f;
    TF_CHECK_INT(mapping_og_map_update(&m, &pose, &pt, 1, 3.0f), 0);
    /* 射线 (1,1)->(4,1)：中间与终点均标记为空闲 */
    TF_CHECK_INT(s_cells[1 * 5 + 2], -40);
    TF_CHECK_INT(s_cells[1 * 5 + 3], -40);
    TF_CHECK_INT(s_cells[1 * 5 + 4], -40);
    TF_CHECK_NEAR(mapping_og_map_probability(&m, 2, 1), 0.4013f, 1e-3);
    TF_CHECK_NEAR(mapping_og_map_probability(&m, 1, 1), 0.5f, 1e-5);
    tf_end();

    tf_begin("robot/og_map 射线终点越界截断");
    fill_map(&m, s_cells, 5, 5);
    pose.x = 4.5f;
    pose.y = 4.5f;
    /* 45 度方向保证终点 x/y 同时越界，两个分量都被截断到地图边界 */
    pose.theta = 0.785398f;
    pt.angle = 0.0f;
    pt.range = 0.0f;
    TF_CHECK_INT(mapping_og_map_update(&m, &pose, &pt, 1, 5.0f), 0);
    TF_CHECK_INT(s_cells[4 * 5 + 4], -40);
    tf_end();

    tf_begin("robot/og_map log-odds 饱和");
    fill_map(&m, s_cells, 5, 5);
    pose.x = 0.5f;
    pose.y = 0.5f;
    pose.theta = 0.0f;
    pt.angle = 0.0f;
    pt.range = 0.5f;
    {
        int i;
        for (i = 0; i < 6; i++) {
            TF_CHECK_INT(mapping_og_map_update(&m, &pose, &pt, 1, 5.0f), 0);
        }
    }
    TF_CHECK_INT(s_cells[0 * 5 + 1], 400);
    TF_CHECK_NEAR(mapping_og_map_probability(&m, 1, 0), 0.9820f, 1e-3);
    tf_end();

    tf_begin("robot/og_map 更新错误路径");
    fill_map(&m, s_cells, 5, 5);
    pose.x = 1.5f;
    pose.y = 1.5f;
    pose.theta = 0.0f;
    pt.angle = 0.0f;
    pt.range = 0.5f;
    TF_CHECK_INT(mapping_og_map_update(NULL, &pose, &pt, 1, 5.0f), -1);
    TF_CHECK_INT(mapping_og_map_update(&m, NULL, &pt, 1, 5.0f), -1);
    TF_CHECK_INT(mapping_og_map_update(&m, &pose, NULL, 1, 5.0f), -1);
    pose.x = -5.0f;
    pose.y = -5.0f;
    TF_CHECK_INT(mapping_og_map_update(&m, &pose, &pt, 1, 5.0f), -2);
    pose.x = 1.5f;
    pose.y = 1.5f;
    TF_CHECK_INT(mapping_og_map_update(&m, &pose, &pt, 0, 5.0f), 0);
    TF_CHECK_INT(s_cells[1 * 5 + 2], 0);
    tf_end();

    tf_begin("robot/og_map 二值导出");
    fill_map(&m, s_cells, 5, 5);
    s_cells[1 * 5 + 1] = 85;   /* p ≈ 0.70 */
    memset(s_out, 0xff, sizeof(s_out));
    TF_CHECK_INT(mapping_og_map_export(&m, s_out, 0.5f), 0);
    TF_CHECK_INT(s_out[1 * 5 + 1], 1);
    TF_CHECK_INT(s_out[0], 0);
    TF_CHECK_INT(s_out[24], 0);
    TF_CHECK_INT(mapping_og_map_export(&m, s_out, 0.9f), 0);
    TF_CHECK_INT(s_out[1 * 5 + 1], 0);
    TF_CHECK_INT(mapping_og_map_export(NULL, s_out, 0.5f), -1);
    TF_CHECK_INT(mapping_og_map_export(&m, NULL, 0.5f), -1);
    tf_end();
}
