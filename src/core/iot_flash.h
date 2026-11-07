#ifndef IOT_FLASH_H
#define IOT_FLASH_H

/*
 * 片内 Flash：容量/页大小查询、读、写、擦除与写保护锁定/解锁。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 IOT_INVALID_PARAM（查询类返回默认值）。
 * 写入前必须擦除对应区域，且地址需按 program_size 对齐；
 * 擦除地址需按 erase_size 对齐，否则返回 IOT_INVALID_PARAM。
 */

#include "iot_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Flash 能力标志，可按位或组合 */
typedef enum {
    IOT_FLASH_FLAG_READ = IOT_BIT(0),     /* 支持读 */
    IOT_FLASH_FLAG_WRITE = IOT_BIT(1),    /* 支持写 */
    IOT_FLASH_FLAG_ERASE = IOT_BIT(2)     /* 支持擦除 */
} iot_flash_flags_t;

/* Flash 信息 */
typedef struct {
    uintptr_t start;        /* 可操作区域起始地址 */
    size_t size;            /* 可操作区域总大小（字节） */

    size_t erase_size;      /* 擦除粒度（字节） */
    size_t program_size;    /* 写入粒度（字节），未对齐写入可能失败 */

    uint32_t flags;         /* IOT_FLASH_FLAG_* 位掩码 */
} iot_flash_info_t;

/* Flash 句柄 */
typedef struct iot_flash {
    iot_handle_base_t base;    /* 通用句柄基类，勿移动位置 */
    iot_flash_info_t info;     /* 初始化时填充的区域信息 */
} iot_flash_t;

/*
 * 初始化 Flash 驱动并填充 handle->info。
 * handle : 句柄，不可为 NULL。
 * 返回   : IOT_OK；IOT_INVALID_PARAM；平台不支持返回 IOT_NOT_SUPPORTED。
 */
iot_status_t iot_flash_init(iot_flash_t *handle);

/* 反初始化 Flash 驱动（不擦除数据）；返回 IOT_OK / IOT_INVALID_PARAM */
iot_status_t iot_flash_deinit(iot_flash_t *handle);

/*
 * 获取 Flash 区域信息。
 * info : 输出结构，不可为 NULL。
 * 返回 : IOT_OK；IOT_INVALID_PARAM；未初始化返回 IOT_NOT_READY。
 */
iot_status_t iot_flash_get_info(iot_flash_t *handle, iot_flash_info_t *info);

/*
 * 读取 Flash。
 * address : 起始地址，需落在 [info.start, info.start + info.size) 内；
 * buffer  : 接收缓冲区，不可为 NULL；
 * length  : 字节数。
 * 返回    : IOT_OK；IOT_INVALID_PARAM（越界）；IOT_NOT_READY。
 */
iot_status_t iot_flash_read(iot_flash_t *handle, uintptr_t address,
                              void *buffer, size_t length);

/*
 * 写入 Flash。
 * address : 起始地址，需按 program_size 对齐且已擦除；
 * buffer  : 源数据，不可为 NULL；
 * length  : 字节数。
 * 返回    : IOT_OK；IOT_INVALID_PARAM；未解锁返回 IOT_PERMISSION；
 *           校验失败返回 IOT_CRC_ERROR。
 */
iot_status_t iot_flash_write(iot_flash_t *handle, uintptr_t address,
                               const void *buffer, size_t length);

/*
 * 擦除 Flash。
 * address : 起始地址，需按 erase_size 对齐；
 * length  : 擦除字节数，需为 erase_size 的整数倍。
 * 返回    : IOT_OK；IOT_INVALID_PARAM；未解锁返回 IOT_PERMISSION。
 * 注意    : 擦除期间通常无法取指，必要时需搬到 RAM 执行。
 */
iot_status_t iot_flash_erase(iot_flash_t *handle, uintptr_t address, size_t length);

/* 加锁写保护；返回 IOT_OK / IOT_INVALID_PARAM / IOT_NOT_SUPPORTED */
iot_status_t iot_flash_lock(iot_flash_t *handle);

/* 解锁写保护；返回 IOT_OK / IOT_INVALID_PARAM / IOT_NOT_SUPPORTED */
iot_status_t iot_flash_unlock(iot_flash_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* IOT_FLASH_H */
