/* 占据栅格建图实现：log-odds 概率更新 + Bresenham 射线投射。 */
#include "iot_og_map.h"
#include "iot_logger.h"

#include <math.h>

/* log-odds 增量（×100），命中 +85 / 未命中 -40 */
#define OG_L_OCC   85
#define OG_L_FREE  40
#define OG_L_MIN   (-400)
#define OG_L_MAX   400

static int16_t og_clamp(int32_t v)
{
    if (v > OG_L_MAX) return OG_L_MAX;
    if (v < OG_L_MIN) return OG_L_MIN;
    return (int16_t)v;
}

int iot_og_map_init(iot_og_map_t *map, uint16_t width, uint16_t height,
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

int iot_og_world_to_cell(const iot_og_map_t *map, float wx, float wy,
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

static void og_mark(iot_og_map_t *map, int16_t cx, int16_t cy, bool occupied)
{
    int idx;

    if (cx < 0 || cy < 0 || cx >= map->width || cy >= map->height) {
        return;
    }
    idx = (int)cy * map->width + cx;
    map->cells[idx] = og_clamp((int32_t)map->cells[idx] + (occupied ? OG_L_OCC : -OG_L_FREE));
}

/* 射线投射：从 (x0,y0) 到 (x1,y1) 更新沿线栅格；hit 表示终点是否为实际命中 */
static void og_ray(iot_og_map_t *map, int16_t x0, int16_t y0,
                   int16_t x1, int16_t y1, bool hit)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    int x = x0, y = y0;

    while (x != x1 || y != y1) {
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx)  { err += dx; y += sy; }
        if (x == x1 && y == y1) {
            break;
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

int iot_og_map_update(iot_og_map_t *map, const iot_pose_t *pose,
                      const iot_scan_point_t *points, uint16_t count, float max_range)
{
    int16_t cx0, cy0, cx1, cy1;
    uint16_t i;

    if (map == NULL || pose == NULL || points == NULL) {
        return -1;
    }
    if (iot_og_world_to_cell(map, pose->x, pose->y, &cx0, &cy0) != 0) {
        return -2;   /* 机器人在地图外 */
    }

    for (i = 0; i < count; i++) {
        float a = pose->theta + points[i].angle;
        float r = points[i].range;
        float ex, ey;
        bool hit = (r > 0.0f && r < max_range);
        float use = hit ? r : max_range;

        ex = pose->x + cosf(a) * use;
        ey = pose->y + sinf(a) * use;
        if (iot_og_world_to_cell(map, ex, ey, &cx1, &cy1) != 0) {
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

float iot_og_map_probability(const iot_og_map_t *map, int16_t cx, int16_t cy)
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

int iot_og_map_export(const iot_og_map_t *map, uint8_t *out, float threshold)
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