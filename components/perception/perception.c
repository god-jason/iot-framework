/*
 * perception.c - 扫描缓冲与坐标转换实现
 *
 * 环形缓冲以 head 为写入位置、count 为有效点数：
 *   最旧点索引 = (head + capacity - count) % capacity。
 */

#include "perception.h"

#include <stddef.h>

#include <math.h>

int scan_init(scan_buffer_t *scan, scan_point_t *buf, uint16_t capacity)
{
    if (scan == NULL || buf == NULL || capacity == 0U) {
        return -1;
    }
    scan->buf = buf;
    scan->capacity = capacity;
    scan->head = 0U;
    scan->count = 0U;
    return 0;
}

void scan_clear(scan_buffer_t *scan)
{
    if (scan == NULL) {
        return;
    }
    scan->head = 0U;
    scan->count = 0U;
}

uint16_t scan_push(scan_buffer_t *scan, const scan_point_t *points, uint16_t count)
{
    uint16_t i;
    uint16_t written = 0U;

    if (scan == NULL || points == NULL || count == 0U ||
        scan->capacity == 0U) {
        return 0U;
    }

    for (i = 0U; i < count; i++) {
        scan->buf[scan->head] = points[i];
        scan->head = (uint16_t)((scan->head + 1U) % scan->capacity);
        if (scan->count < scan->capacity) {
            scan->count++;
        }
        written++;
    }
    return written;
}

uint16_t scan_count(const scan_buffer_t *scan)
{
    return (scan != NULL) ? scan->count : 0U;
}

int scan_get(const scan_buffer_t *scan, uint16_t index, scan_point_t *out)
{
    uint16_t pos;

    if (scan == NULL || out == NULL || scan->capacity == 0U ||
        index >= scan->count) {
        return -1;
    }
    pos = (uint16_t)((scan->head + scan->capacity - scan->count + index) %
                     scan->capacity);
    *out = scan->buf[pos];
    return 0;
}

int scan_to_cartesian(const scan_point_t *point, float *x, float *y)
{
    if (point == NULL || x == NULL || y == NULL) {
        return -1;
    }
    *x = point->range * cosf(point->angle);
    *y = point->range * sinf(point->angle);
    return 0;
}

int scan_transform_world(const pose2d_t *pose, const scan_point_t *points,
                         uint16_t count, float *xs, float *ys)
{
    uint16_t i;
    float c;
    float s;

    if (pose == NULL || points == NULL || xs == NULL || ys == NULL) {
        return -1;
    }

    c = cosf(pose->theta);
    s = sinf(pose->theta);
    for (i = 0U; i < count; i++) {
        float px;
        float py;
        if (scan_to_cartesian(&points[i], &px, &py) != 0) {
            return -1;
        }
        xs[i] = pose->x + c * px - s * py;
        ys[i] = pose->y + s * px + c * py;
    }
    return 0;
}

int scan_nearest(const scan_point_t *points, uint16_t count,
                 float min_angle, float max_angle, float *range)
{
    uint16_t i;
    float best = 0.0f;
    int found = 0;

    if (points == NULL || range == NULL || count == 0U) {
        return -1;
    }

    for (i = 0U; i < count; i++) {
        if (points[i].range <= 0.0f) {
            continue;
        }
        if (points[i].angle < min_angle || points[i].angle > max_angle) {
            continue;
        }
        if (!found || points[i].range < best) {
            best = points[i].range;
            found = 1;
        }
    }

    if (!found) {
        return -1;
    }
    *range = best;
    return 0;
}
