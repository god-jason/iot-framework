#ifndef IOT_MACRO_H
#define IOT_MACRO_H

/*
 * IOT 通用宏。
 * 本文件不依赖任何 OS 或厂商 HAL。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 通用宏 ---------------- */
#define IOT_BIT(n)             (1UL << (n))                    /* 第 n 位置 1 */
#define IOT_ARRAY_SIZE(a)      (sizeof(a) / sizeof((a)[0]))    /* 数组元素个数 */
#define IOT_MIN(a, b)          ((a) < (b) ? (a) : (b))         /* 取较小值 */
#define IOT_MAX(a, b)          ((a) > (b) ? (a) : (b))         /* 取较大值 */
#define IOT_ALIGN_UP(v, a)     (((v) + (a) - 1U) & ~((a) - 1U))/* 向上对齐到 a 的整数倍，a 必须为 2 的幂 */
#define IOT_UNUSED(x)          ((void)(x))                     /* 显式忽略未使用参数，避免编译告警 */

#ifdef __cplusplus
}
#endif

#endif /* IOT_MACRO_H */
