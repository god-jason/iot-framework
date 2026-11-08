/*
 * 占据栅格建图实现：log-odds 概率更新 + Bresenham 射线投射。
 *
 * 算法流程：
 *   1) 位姿与每束测距换算成世界坐标下的射线终点；
 *   2) 起点栅格到终点栅格用 Bresenham 走线，途中格子累加“空闲”证据；
 *   3) 终点按是否真实命中累加“占据”或“空闲”证据；
 *   4) 概率由 log-odds 经 sigmoid 反解：p = 1 - 1/(1+e^l)。
 *
 * 边界与注意事项：
 *   - 所有写入都先做栅格范围检查，越界直接丢弃；
 *   - log-odds 累加后钳位到 [OG_L_MIN, OG_L_MAX]，避免有符号溢出与饱和不可逆；
 *   - 射线终点越界时仅对越界的一侧做钳位（保持既有行为，见 mapping_og_map_update）。
 */
#include "mapping.h"
#include "log.h"

#include <math.h>

/* log-odds 增量（×100），命中 +85 / 未命中 -40 */
#define OG_L_OCC   85       /* 命中占据格的 log-odds 增量 */
#define OG_L_FREE  40       /* 空闲格的 log-odds 减少量 */
#define OG_L_MIN   (-400)   /* log-odds 下限（×100） */
#define OG_L_MAX   400      /* log-odds 上限（×100），防止累加溢出 */

/*
 * 把 int32_t 的累加结果钳位到 int16_t 存储范围。
 * @param v 累加后的 log-odds（×100）
 * @return 钳位后的值
 */
static int16_t og_clamp(int32_t v)
{
    if (v > OG_L_MAX) return OG_L_MAX;
    if (v < OG_L_MIN) return OG_L_MIN;
    return (int16_t)v;
}

/*
 * 初始化地图：保存元数据并把所有栅格置 0（未知，概率 0.5）。
 * @param map 地图对象
 * @param width/height 栅格尺寸，必须非 0
 * @param resolution 米/格，必须 >0
 * @param origin_x/origin_y 左下角世界坐标
 * @param cells 外部存储，长度需 >= width*height
 * @return 0 成功；-1 参数非法
 */
int mapping_og_map_init(mapping_og_map_t *map, uint16_t width, uint16_t height,
                    float resolution, float origin_x, float origin_y, int16_t *cells)
{
    int i;

    if (map == NULL || cells == NULL || width == 0 || height == 0 || resolution <= 0.0f) {
        return -1;
    }
    map->width = width;
    map->height = height;
    map->resolution = resolution;
    map->origin_x = origin_x;
    map->origin_y = origin_y;
    map->cells = cells;

    for (i = 0; i < (int)width * height; i++) {
        cells[i] = 0;   /* 未知 = 0（概率 0.5） */
    }
    return 0;
}

/*
 * 世界坐标转栅格索引：ix = (int)((wx - origin_x) / resolution)，即向 0 截断取整。
 * @param cx/cy 输出索引，不做 NULL 检查（调用方保证有效）
 * @return 0 成功；-1 表示 map 为 NULL 或坐标在地图范围外
 * @note 负数坐标经 int 截断后为负值，会被范围检查拒绝。
 */
int mapping_world_to_cell(const mapping_og_map_t *map, float wx, float wy,
                         int16_t *cx, int16_t *cy)
{
    int ix, iy;

    if (map == NULL) {
        return -1;
    }
    ix = (int)((wx - map->origin_x) / map->resolution);
    iy = (int)((wy - map->origin_y) / map->resolution);
    if (ix < 0 || iy < 0 || ix >= map->width || iy >= map->height) {
        return -1;
    }
    *cx = (int16_t)ix;
    *cy = (int16_t)iy;
    return 0;
}

/*
 * 更新单个栅格的 log-odds。
 * @param occupied true 累加占据证据（+OG_L_OCC），false 累加空闲证据（-OG_L_FREE）
 * @note 越界静默忽略；索引按行优先 cy*width+cx 计算。
 */
static void og_mark(mapping_og_map_t *map, int16_t cx, int16_t cy, bool occupied)
{
    int idx;

    if (cx < 0 || cy < 0 || cx >= map->width || cy >= map->height) {
        return;
    }
    idx = (int)cy * map->width + cx;
    map->cells[idx] = og_clamp((int32_t)map->cells[idx] + (occupied ? OG_L_OCC : -OG_L_FREE));
}

/*
 * Bresenham 射线投射：把 (x0,y0)→(x1,y1) 途经的栅格标记为空闲，终点按 hit 标记。
 * 起点所在格不标记（机器人自身位置不代表可通行证据）。
 * @param hit true 表示终点是真实命中（占据），false 表示超量程（空闲）
 * @note 采用整数误差累积的经典 Bresenham，无浮点运算。
 */
static void og_ray(mapping_og_map_t *map, int16_t x0, int16_t y0,
                   int16_t x1, int16_t y1, bool hit)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    int x = x0, y = y0;

    while (x != x1 || y != y1) {
        int e2 = 2 * err;                       /* 误差放大 2 倍，避免除法/浮点 */
        if (e2 > -dy) { err -= dy; x += sx; }   /* 误差偏向 x：沿 x 走一步 */
        if (e2 < dx)  { err += dx; y += sy; }   /* 误差偏向 y：沿 y 走一步（对角时同时走） */
        if (x == x1 && y == y1) {
            break;                              /* 终点留给循环外单独标记 */
        }
        if (!(x == x0 && y == y0)) {
            og_mark(map, (int16_t)x, (int16_t)y, false);   /* 中间无障碍 */
        }
    }
    /* 终点：命中则占据，否则（超量程）也是无障碍 */
    if (hit) {
        og_mark(map, x1, y1, true);
    } else {
        og_mark(map, x1, y1, false);
    }
}

/*
 * 用一帧扫描更新地图。
 * @param pose      机器人位姿（世界坐标），不可为 NULL
 * @param points    测距点数组（角度为相对朝向），不可为 NULL
 * @param count     测距点数量
 * @param max_range 量程上限；range<=0 或 range>=max_range 视为未命中
 * @return 0 成功；-1 参数为 NULL；-2 机器人位姿落在地图外
 *
 * 处理流程：把每束射线换算为世界坐标终点后交给 og_ray 更新证据。
 * 注意：终点越界时先做逐轴钳位；若只有一轴越界，另一轴沿用
 * mapping_world_to_cell 未写入的本地变量值（既有实现行为，未做修改）。
 */
int mapping_og_map_update(mapping_og_map_t *map, const mapping_pose_t *pose,
                      const mapping_scan_point_t *points, uint16_t count, float max_range)
{
    int16_t cx0, cy0, cx1, cy1;
    uint16_t i;

    if (map == NULL || pose == NULL || points == NULL) {
        return -1;
    }
    if (mapping_world_to_cell(map, pose->x, pose->y, &cx0, &cy0) != 0) {
        return -2;   /* 机器人在地图外 */
    }

    for (i = 0; i < count; i++) {
        float a = pose->theta + points[i].angle;    /* 世界坐标系下的射线方向 */
        float r = points[i].range;
        float ex, ey;
        bool hit = (r > 0.0f && r < max_range);     /* 有效量程内才算命中 */
        float use = hit ? r : max_range;            /* 未命中时按量程上限投射 */

        ex = pose->x + cosf(a) * use;
        ey = pose->y + sinf(a) * use;
        if (mapping_world_to_cell(map, ex, ey, &cx1, &cy1) != 0) {
            /* 终点越界：把坐标钳到地图边界内，保证射线仍能更新边界附近栅格 */
            cx1 = (ex >= map->origin_x + map->width * map->resolution)
                  ? map->width - 1 : ((ex < map->origin_x) ? 0 : cx1);
            cy1 = (ey >= map->origin_y + map->height * map->resolution)
                  ? map->height - 1 : ((ey < map->origin_y) ? 0 : cy1);
        }
        og_ray(map, cx0, cy0, cx1, cy1, hit);
    }
    LOG_I("ogmap", "scan %u pts updated", (unsigned)count);
    return 0;
}

/*
 * 由 log-odds 反解占用概率：l = cells/100，p = 1 - 1/(1+e^l)。
 * l=0 → p=0.5；l 很大 → p→1；l 很小 → p→0。
 * @return 概率 [0,1]；越界或 map 为 NULL 返回 0.5
 */
float mapping_og_map_probability(const mapping_og_map_t *map, int16_t cx, int16_t cy)
{
    int idx;
    float l;

    if (map == NULL || cx < 0 || cy < 0 || cx >= map->width || cy >= map->height) {
        return 0.5f;
    }
    idx = (int)cy * map->width + cx;
    l = (float)map->cells[idx] / 100.0f;
    return 1.0f - 1.0f / (1.0f + expf(l));
}

/*
 * 导出二值栅格图：概率 > threshold 的格子写 1（障碍），其余写 0（可通行）。
 * @param out       输出数组，长度需 >= width*height
 * @param threshold 判定阈值（0~1）
 * @return 0 成功；-1 参数为 NULL
 * @note 逐格重复计算 sigmoid（与 probability 接口一致），未做查表优化。
 */
int mapping_og_map_export(const mapping_og_map_t *map, uint8_t *out, float threshold)
{
    int i, n;

    if (map == NULL || out == NULL) {
        return -1;
    }
    n = (int)map->width * map->height;
    for (i = 0; i < n; i++) {
        float l = (float)map->cells[i] / 100.0f;
        float p = 1.0f - 1.0f / (1.0f + expf(l));
        out[i] = (p > threshold) ? 1 : 0;
    }
    return 0;
}