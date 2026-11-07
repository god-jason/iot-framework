#ifndef DRIVER_TYPES_H
#define DRIVER_TYPES_H

/*
 * 驱动公共类型定义。
 *
 * 设计约定（参考 libdriver）：
 *   - 每个器件一个目录 drivers/<chip>/，包含 <chip>.h、<chip>_register.h、<chip>.c
 *   - 公开 API 统一为 <chip>_init / <chip>_deinit / <chip>_read / <chip>_write ...
 *   - 所有返回值使用 int：0 表示成功，负值为 iot_status_t
 *   - 所有硬件访问只经由 src/core 的抽象接口，不直接操作寄存器
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "iot.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 返回值 ---------------- */
#define DRIVER_OK               ((int)IOT_OK)

/* ---------------- 通用宏 ---------------- */
#define DRIVER_UNUSED(x)        ((void)(x))
#define DRIVER_ARRAY_SIZE(a)    (sizeof(a) / sizeof((a)[0]))
#define DRIVER_BIT(n)           (1UL << (n))
#define DRIVER_MIN(a, b)        ((a) < (b) ? (a) : (b))
#define DRIVER_MAX(a, b)        ((a) > (b) ? (a) : (b))
#define DRIVER_CLAMP(v, lo, hi) (DRIVER_MIN(DRIVER_MAX((v), (lo)), (hi)))

/* ---------------- I2C 设备描述 ---------------- */
typedef struct {
    iot_i2c_t *handle;   /* 已初始化的 I2C 主机句柄 */
    uint16_t address;            /* 7 位从机地址（不含读写位） */
    uint32_t timeout_ms;         /* 单次传输超时 */
} driver_i2c_t;

/* ---------------- SPI 设备描述 ---------------- */
typedef struct {
    iot_spi_t *handle;   /* 已初始化的 SPI 主机句柄 */
    iot_pin_t cs;               /* 片选引脚，IOT_PIN(0,0) 表示不使用 */
    uint32_t timeout_ms;
} driver_spi_t;

/* ---------------- 常用延时 ---------------- */
#define DRIVER_I2C_TIMEOUT_DEFAULT  100U
#define DRIVER_SPI_TIMEOUT_DEFAULT  100U

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_TYPES_H */
