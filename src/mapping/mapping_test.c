/*
 * mapping_test.c - 占据栅格建图测试（对应 mapping.c）
 *
 * 覆盖：地图初始化、世界坐标到栅格、单帧扫描更新（空闲/占据）、
 *       概率读取、二值化导出、非法参数。
 */

#include "test.h"
#include "mapping.h"

void ut_robot_og_map(void)
{
    enum { W = 9, H = 9 };
    int16_t cells[W * H];
    uint8_t out[W * H];
    mapping_og_map_t map;
    mapping_pose_t pose;
    mapping_scan_point_t points[3];
    int16_t cx = 0;
    int16_t cy = 0;

    tf_begin("mapping/初始化");

    TF_CHECK_INT(mapping_og_map_init(NULL, W, H, 0.1f, 0.0f, 0.0f, cells),
                 -1);
    TF_CHECK_INT(mapping_og_map_init(&map, 0U, H, 0.1f, 0.0f, 0.0f, cells),
                 -1);
    TF_CHECK_INT(mapping_og_map_init(&map, W, H, 0.0f, 0.0f, 0.0f, cells),
                 -1);
    TF_CHECK_INT(mapping_og_map_init(&map, W, H, 0.1f, 0.0f, 0.0f, NULL),
                 -1);
    TF_CHECK_INT(mapping_og_map_init(&map, W, H, 0.1f, 0.0f, 0.0f, cells),
                 0);
    TF_CHECK_INT(map.width, W);
    TF_CHECK_INT(map.height, H);

    /* 初始化后全部为未知（概率 0.5） */
    TF_CHECK_NEAR(mapping_og_map_probability(&map, 0, 0), 0.5f, 1e-6);
    TF_CHECK_NEAR(mapping_og_map_probability(&map, 8, 8), 0.5f, 1e-6);
    TF_CHECK_NEAR(mapping_og_map_probability(&map, -1, 0), 0.5f, 1e-6);

    tf_end();

    tf_begin("mapping/坐标转换");

    TF_CHECK_INT(mapping_world_to_cell(&map, 0.0f, 0.0f, &cx, &cy), 0);
    TF_CHECK_INT(cx, 0);
    TF_CHECK_INT(cy, 0);

    TF_CHECK_INT(mapping_world_to_cell(&map, 0.25f, 0.35f, &cx, &cy), 0);
    TF_CHECK_INT(cx, 2);
    TF_CHECK_INT(cy, 3);

    /* 地图外 */
    TF_CHECK_INT(mapping_world_to_cell(&map, 10.0f, 0.0f, &cx, &cy), -1);
    TF_CHECK_INT(mapping_world_to_cell(NULL, 0.0f, 0.0f, &cx, &cy), -1);

    tf_end();

    tf_begin("mapping/扫描更新");

    pose.x = 0.45f;
    pose.y = 0.45f;
    pose.theta = 0.0f;

    /* 三束：正前方 0.3m（命中）、斜向 0.6m、正后方 0.2m */
    points[0].angle = 0.0f;
    points[0].range = 0.3f;
    points[1].angle = 0.785398f;
    points[1].range = 0.6f;
    points[2].angle = 3.14159f;
    points[2].range = 0.2f;

    TF_CHECK_INT(mapping_og_map_update(&map, &pose, points, 3U, 5.0f), 0);

    /* 射线终点概率高于 0.5（占据），起点附近低于 0.5（空闲） */
    TF_CHECK(mapping_og_map_probability(&map, 7, 4) > 0.5f);
    TF_CHECK(mapping_og_map_probability(&map, 5, 4) < 0.5f);

    TF_CHECK_INT(mapping_og_map_update(NULL, &pose, points, 3U, 5.0f), -1);
    TF_CHECK_INT(mapping_og_map_update(&map, NULL, points, 3U, 5.0f), -1);
    TF_CHECK_INT(mapping_og_map_update(&map, &pose, NULL, 3U, 5.0f), -1);

    /* 位姿在地图外 */
    pose.x = 100.0f;
    TF_CHECK_INT(mapping_og_map_update(&map, &pose, points, 3U, 5.0f), -2);

    tf_end();

    tf_begin("mapping/二值化导出");

    TF_CHECK_INT(mapping_og_map_export(&map, out, 0.6f), 0);
    TF_CHECK_INT(out[4 * W + 7], 1);        /* 已知占据 */
    TF_CHECK_INT(out[4 * W + 5], 0);        /* 已知空闲 */
    TF_CHECK_INT(out[0], 0);                /* 未知且阈值 > 0.5 => 可通行 */

    TF_CHECK_INT(mapping_og_map_export(NULL, out, 0.6f), -1);
    TF_CHECK_INT(mapping_og_map_export(&map, NULL, 0.6f), -1);

    tf_end();
}
