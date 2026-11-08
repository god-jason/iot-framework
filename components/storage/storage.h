#ifndef STORAGE_H
#define STORAGE_H

/*
 * 存储后端注册表：按名管理多个存储介质（内部 Flash 区 / 外部 SPI Flash / EEPROM）。
 *
 * 模块说明：
 *   - 上层只依赖「名称 + 地址 + 数据 + 长度」，不感知具体介质；
 *   - 具体介质通过 storage_ops_t 回调注入，ctx 用于携带驱动私有数据；
 *   - 注册表为固定容量（实现中为 8），名称仅保存指针、不拷贝。
 * 使用约束：
 *   - 注册的 name 必须在后端生命周期内保持有效；
 *   - 所有回调的返回值约定 0（0）成功，其余为错误码。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 存储后端操作函数表；除 ctx 外回调均可为 NULL，未实现的接口调用会失败 */
typedef struct {
    int (*init)(void *ctx);                                    /* 初始化后端，注册时自动调用一次 */
    int (*read)(void *ctx, uint32_t addr, void *data, uint32_t len);              /* 读数据 */
    int (*write)(void *ctx, uint32_t addr, const void *data, uint32_t len);       /* 写数据 */
    int (*erase)(void *ctx, uint32_t addr, uint32_t len);       /* 擦除区域（长度通常需对齐） */
    void *ctx;                                                  /* 驱动私有上下文，原样回传给回调 */
} storage_ops_t;

/*
 * 注册后端。
 * 参数：name 后端名称（不可为 NULL）；ops 操作表（不可为 NULL）。
 * 返回：0 成功；参数非法或注册表已满返回 -1。
 */
int storage_register(const char *name, const storage_ops_t *ops);

/*
 * 注销后端。
 * 参数：name 后端名称。
 * 返回：0 成功；名称为 NULL 或未找到返回 -1。
 */
int storage_unregister(const char *name);

/*
 * 读取数据。
 * 参数：name 后端名称；addr 起始地址；data 输出缓冲；len 字节数。
 * 返回：后端返回值；后端不存在或未实现 read 时返回 -1。
 */
int storage_read(const char *name, uint32_t addr, void *data, uint32_t len);

/*
 * 写入数据。
 * 参数：name 后端名称；addr 起始地址；data 源数据；len 字节数。
 * 返回：后端返回值；后端不存在或未实现 write 时返回 -1。
 */
int storage_write(const char *name, uint32_t addr, const void *data, uint32_t len);

/*
 * 擦除数据。
 * 参数：name 后端名称；addr 起始地址；len 字节数。
 * 返回：后端返回值；后端不存在或未实现 erase 时返回 -1。
 */
int storage_erase(const char *name, uint32_t addr, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_H */